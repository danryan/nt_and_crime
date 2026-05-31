// ENVGEN (vendor APP_ENVGEN.h) O_C app port. Quad-channel app: the vendor object
// is the file-scope singleton QuadEnvelopeGenerator `envgen`, holding four
// EnvelopeGenerator SettingsBase instances of 33 settings each, so the settings
// facade is the BBGEN quad facade dispatching 132 logical rows across the four
// channels (idx/33 = channel, idx%33 = setting). Validates the real app through
// the firmware factory lifecycle via the shared plugin_loader.
//
// Only plugins/apps/ENVGEN.cpp (NT_OC_APP_TU) aggregates the OC shim impl; this
// TU links it and reaches the embedded state through the test seams below.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "oc_ui_sim.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_core.h"

#include <cstring>
#include <string>

// Test seams defined in plugins/apps/ENVGEN.cpp.
int  envgen_get_setting(int channel, int setting);
bool envgen_apply_setting(int channel, int setting, int value);
int  envgen_setting_count();          // 4 * ENV_SETTING_LAST == 132
int  envgen_settings_per_channel();   // ENV_SETTING_LAST == 33
int  envgen_settings_param_base();
const char* envgen_param_name(int idx);  // channel-prefixed NT row name
void envgen_arm_sentinel(_NT_algorithm* self);

namespace {
// Vendor EnvSettings enum order (APP_ENVGEN.h:43). Mirrored locally so the test
// can name fields without including the vendor header.
enum {
    ENV_TYPE = 0, ENV_SEG1, ENV_SEG2, ENV_SEG3, ENV_SEG4, ENV_TRIGGER_INPUT,
    ENV_LAST_NAMED  // not the real LAST; only the head fields are named here
};

void run_steps(nt::LoadedPlugin* p, int numFrames, int steps) {
    for (int s = 0; s < steps; ++s)
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
}
}  // namespace

TEST_CASE("ENVGEN loads through the factory path with a custom UI", "[oc_app][envgen][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);
    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'E', 'G'));
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);

    REQUIRE(envgen_settings_per_channel() == 33);
    REQUIRE(envgen_setting_count() == 132);
}

TEST_CASE("ENVGEN exposes 144 parameters with channel-prefixed setting names", "[oc_app][envgen][params]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // 12 I/O routing rows + 132 settings = 144. The param base is the I/O row
    // count; the flat settings table extends from there.
    REQUIRE(envgen_settings_param_base() == 12);
    REQUIRE(envgen_setting_count() == 132);

    // Channel-prefixed names: row 0 is channel A's TYPE, row 33 is channel B's.
    REQUIRE(envgen_param_name(0)[0] == 'A');
    REQUIRE(envgen_param_name(33)[0] == 'B');
    REQUIRE(envgen_param_name(66)[0] == 'C');
    REQUIRE(envgen_param_name(99)[0] == 'D');
}

TEST_CASE("ENVGEN draw renders the menu", "[oc_app][envgen][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);
    int nonzero = 0;
    for (int i = 0; i < 128 * 64; ++i) if (NT_screen[i] != 0) ++nonzero;
    REQUIRE(nonzero > 0);
}

TEST_CASE("ENVGEN gate-triggered envelope outputs within 0V..+5V and moves", "[oc_app][envgen][isr]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Channel A's default trigger is DIGITAL_INPUT_1 -> TR in 1 -> default bus 5;
    // its output is CV out A -> default bus 13. Channel C defaults to
    // DIGITAL_INPUT_3 -> TR in 3 -> bus 7 (left low) and outputs CV out C -> bus
    // 15. Default amplitude is 127 (full), so a gate produces a non-zero unipolar
    // envelope. The ungated channel C stays at 0V (per-channel gate routing).
    float* trig1 = nt::bus_pointer(5, numFrames);
    float* outA  = nt::bus_pointer(13, numFrames);
    float* outC  = nt::bus_pointer(15, numFrames);
    for (int i = 0; i < numFrames; ++i) trig1[i] = 5.0f;

    run_steps(p, numFrames, 40);

    // Ungated channel C output never leaves 0V across the run.
    for (int s = 0; s < 40; ++s) {
        run_steps(p, numFrames, 1);
        REQUIRE(outC[0] == Catch::Approx(0.0f).margin(0.05f));
    }

    // Channel A output must move (the envelope is dynamic) and stay within the
    // unipolar 0V..+5V rails (modulation code space, not railed by a wrong
    // /1536 pitch conversion).
    const float sample0 = outA[0];
    bool moved = false;
    for (int s = 0; s < 120 && !moved; ++s) {
        run_steps(p, numFrames, 1);
        if (outA[0] != Catch::Approx(sample0).margin(1e-6)) moved = true;
    }
    REQUIRE(moved);
    for (int s = 0; s < 80; ++s) {
        run_steps(p, numFrames, 1);
        REQUIRE(outA[0] >= -0.1f);
        REQUIRE(outA[0] <= 5.1f);
    }
}

TEST_CASE("ENVGEN settings round-trip through factory serialise/deserialise", "[oc_app][envgen][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Distinct in-range values across each channel's segment values (U16 0..255).
    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(envgen_apply_setting(ch, ENV_SEG1, 50 + ch * 10));
        REQUIRE(envgen_apply_setting(ch, ENV_SEG2, 200 - ch * 10));
    }

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (int ch = 0; ch < 4; ++ch) {
        envgen_apply_setting(ch, ENV_SEG1, 0);
        envgen_apply_setting(ch, ENV_SEG2, 0);
    }
    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(envgen_get_setting(ch, ENV_SEG1) == 50 + ch * 10);
        REQUIRE(envgen_get_setting(ch, ENV_SEG2) == 200 - ch * 10);
    }
}

TEST_CASE("ENVGEN NT parameter add-on syncs from the parameter store", "[oc_app][envgen][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = envgen_settings_param_base();
    envgen_arm_sentinel(p->algorithm);

    // NT parameter -> app value: write channel B's SEG1 (row base + 33 + ENV_SEG1)
    // into alg->v and fire parameterChanged; the app value must follow.
    const int row = base + 33 + ENV_SEG1;
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[row] = 77;
    p->factory->parameterChanged(p->algorithm, row);
    REQUIRE(envgen_get_setting(1, ENV_SEG1) == 77);
}
