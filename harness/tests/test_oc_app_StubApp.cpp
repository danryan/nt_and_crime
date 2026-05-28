// Foundation gate: proves the O_C-app build path end-to-end with a minimal
// throwaway stub app. The stub exercises the same factory lifecycle a real
// O_C-app plug-in does (calculateRequirements -> construct -> step -> draw ->
// customUi -> serialise/deserialise), loaded through the shared plugin_loader
// factory path exactly as the device firmware would.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/StubApp.cpp
// (which defines NT_OC_APP_TU at its top) aggregates; this test TU links the
// shim sources separately is NOT required because StubApp.cpp supplies every
// shim symbol via its single aggregating TU. The test merely includes the
// harness and the per-app runtime header (without NT_OC_APP_TU, so the include
// of oc_shim_impl.h is suppressed here and the duplicate-symbol hazard is
// avoided).

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "oc_ui_sim.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"

#include <cstring>
#include <cstdint>
#include <string>

// Test seams defined in plugins/apps/StubApp.cpp. These expose the embedded
// settings instance so the test can mutate and read values without pulling the
// concrete settings type (and its SETTINGS_DECLARE specialization) into this
// TU, which would risk an ODR clash with the aggregating .cpp.
int  stub_app_get_setting(_NT_algorithm* self, int idx);
bool stub_app_apply_setting(_NT_algorithm* self, int idx, int value);
int  stub_app_setting_count();
int  stub_app_settings_param_base();
// Returns true if the menu draw thunk ran at least once.
int  stub_app_loop_calls(_NT_algorithm* self);

namespace {

// Count non-zero pixels in NT_screen (the stub menu draws a string; any
// non-zero byte proves something reached the display).
int count_nonzero_screen() {
    int n = 0;
    for (int i = 0; i < 128 * 64; ++i) {
        if (NT_screen[i] != 0) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("StubApp loads through the factory path", "[oc_app][stub][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    // The factory advertises the stub's GUID and a custom UI surface.
    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'S', 'b'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->hasCustomUi != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);
}

TEST_CASE("StubApp step and draw run without crashing and the menu draws", "[oc_app][stub][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Drive several audio buffers: step() runs the cadence accumulator and the
    // app isr; nothing should fault.
    for (int s = 0; s < 4; ++s) {
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
    }
    REQUIRE(stub_app_loop_calls(p->algorithm) == 0);  // loop runs in draw, not step

    // draw() runs the app loop + DrawMenu and centers the canvas. The stub
    // draws a string, so the screen must carry non-zero pixels afterward.
    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    REQUIRE(stub_app_loop_calls(p->algorithm) >= 1);
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("StubApp customUi forwards control events without crashing", "[oc_app][stub][customui]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    auto* alg = static_cast<oc_runtime::AppAlgorithm*>(p->algorithm);

    // Arm the sentinel by drawing once (mirrors the real lifecycle).
    p->factory->draw(p->algorithm);

    // Short press on the left encoder button: the per-app customUi must route
    // it to HandleButtonEvent. We assert only that the call path is exercised
    // without crashing; behavior coverage of real apps lives in their tasks.
    oc_ui_sim::press(*alg, kNT_encoderButtonL);

    // An encoder turn must be accepted too.
    oc_ui_sim::turn_encoder(*alg, oc_ui_sim::ENCODER_L, +1);

    // Drive a buffer to be sure the post-event state is consistent.
    nt::set_bus_frame_count(32);
    p->factory->step(p->algorithm, nt::bus_frames_base(), 8);
    SUCCEED("customUi + step ran without faulting");
}

TEST_CASE("StubApp settings round-trip through factory serialise/deserialise", "[oc_app][stub][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(stub_app_setting_count() >= 2);

    // Mutate the embedded settings to non-default values.
    REQUIRE(stub_app_apply_setting(p->algorithm, 0, 7));
    REQUIRE(stub_app_apply_setting(p->algorithm, 1, 2));
    REQUIRE(stub_app_get_setting(p->algorithm, 0) == 7);
    REQUIRE(stub_app_get_setting(p->algorithm, 1) == 2);

    // serialise into a host JSON stream (addNumber-only format).
    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    // Reset the embedded settings to a different value, then deserialise the
    // saved blob: the original values must be restored.
    REQUIRE(stub_app_apply_setting(p->algorithm, 0, 1));
    REQUIRE(stub_app_apply_setting(p->algorithm, 1, 0));
    REQUIRE(stub_app_get_setting(p->algorithm, 0) == 1);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    REQUIRE(stub_app_get_setting(p->algorithm, 0) == 7);
    REQUIRE(stub_app_get_setting(p->algorithm, 1) == 2);
}
