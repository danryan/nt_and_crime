// Per-applet pilot test: ProbabilityDivider.
//
// Manifest: shim/include/applet_manifests/ProbabilityDivider.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ProbabilityDivider.h
//
// Coverage shape (per CLAUDE.md "PD3/PD4" canonical template):
//   Round-trip + state-injection only. Bus-level fire-count assertions are
//   dropped because (a) ProbabilityDivider's output depends on a random
//   weighted draw, and (b) even with fixed weights the 10x clocked-multiplier
//   rule makes per-edge output counts unobservable. Tests confirm:
//     - Factory entry and construct work correctly.
//     - OnDataRequest() default state after BaseStart().
//     - Round-trip through serialise/deserialise preserves weights,
//       loop_length, and seed. Singleton state (seed) survives within
//       the same .o instance.
//     - Encoder turn advances via on_encoder_turn without crash.
//     - Button-press and aux-button via customUi do not crash.
//
// Singleton-private-to-.o note:
//   HSProbLoopLinker::instance is defined in ProbabilityDivider.cpp's
//   translation unit only. Singleton state is therefore independent of any
//   other per-applet .o that might include HSProbLoopLinker.h. Tests confirm
//   round-trip within a single .o instance.
//
// ODR discipline:
//   This test file does NOT include ProbabilityDivider.h or hemispheres_shim.h.
//   Both are included by plugins/applets/ProbabilityDivider.cpp which is
//   compiled into the same binary. Including them here would cause duplicate
//   definitions of ProbLoopLinker::instance and shim globals.
//   State introspection uses the factory serialise/deserialise hooks and the
//   opaque state accessor declared below.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "nt_jsonstream.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstdint>
#include <cstring>

// Opaque state accessors defined in ProbabilityDivider.cpp. Both TUs
// compile into the same binary. The functions use the complete types without
// requiring this TU to include the vendor or shim headers.
extern "C" {
uint64_t get_pd_state(_NT_algorithm* alg);
void     set_pd_state(_NT_algorithm* alg, uint64_t state);
}

// Helper: pack the 40-bit state word matching ProbabilityDivider::OnDataRequest.
//   bits [0,4)   = weight_1  (0..15)
//   bits [4,4)   = weight_2
//   bits [8,4)   = weight_4
//   bits [12,4)  = weight_8
//   bits [16,8)  = loop_length (0..32)
//   bits [24,16) = seed (16-bit)
static uint64_t pack_pd(int weight_1, int weight_2, int weight_4,
                        int weight_8, int loop_length, int seed) {
    uint64_t data = 0;
    data |= (uint64_t)(weight_1    & 0x0F);
    data |= (uint64_t)(weight_2    & 0x0F) << 4;
    data |= (uint64_t)(weight_4    & 0x0F) << 8;
    data |= (uint64_t)(weight_8    & 0x0F) << 12;
    data |= (uint64_t)(loop_length & 0xFF) << 16;
    data |= (uint64_t)(seed        & 0xFFFF) << 24;
    return data;
}

// Shared setup: reset the runtime, load the per-applet plugin, run one step
// to initialize frame state, return the loaded plugin.
static nt::LoadedPlugin* setup_per_applet() {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus = nt::bus_frames_base();
    int    nBy4 = nt::bus_frame_count() / 4;
    loaded->factory->step(loaded->algorithm, bus, nBy4);
    return loaded;
}

TEST_CASE("ProbabilityDivider PA1: factory entry resolves correctly",
          "[per-applet-pilot][probabilitydivider]") {
    nt::reset_runtime();
    uintptr_t v = pluginEntry(kNT_selector_version, 0);
    REQUIRE(v == kNT_apiVersionCurrent);
    uintptr_t n = pluginEntry(kNT_selector_numFactories, 0);
    REQUIRE(n == 1);
    uintptr_t f = pluginEntry(kNT_selector_factoryInfo, 0);
    REQUIRE(f != 0);
    const auto* factory = reinterpret_cast<const _NT_factory*>(f);
    REQUIRE(factory->guid == NT_MULTICHAR('H','m','P','d'));
}

TEST_CASE("ProbabilityDivider PA2: load_plugin constructs instance and populates ABI fields",
          "[per-applet-pilot][probabilitydivider]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    // HemiPluginInterface fields populated by construct().
    auto* iface = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(iface->magic == kHemiInterfaceMagic);
    REQUIRE(iface->interface_version == kHemiInterfaceVersion);
    REQUIRE(iface->render_view       != nullptr);
    REQUIRE(iface->on_encoder_turn   != nullptr);
    REQUIRE(iface->on_button_press   != nullptr);
    REQUIRE(iface->on_aux_button     != nullptr);
}

TEST_CASE("ProbabilityDivider PA3: Start() defaults all weights=0, loop_length=0",
          "[per-applet-pilot][probabilitydivider]") {
    auto* loaded = setup_per_applet();
    uint64_t data = get_pd_state(loaded->algorithm);
    REQUIRE((int)((data >> 0)  & 0x0F) == 0);  // weight_1
    REQUIRE((int)((data >> 4)  & 0x0F) == 0);  // weight_2
    REQUIRE((int)((data >> 8)  & 0x0F) == 0);  // weight_4
    REQUIRE((int)((data >> 12) & 0x0F) == 0);  // weight_8
    REQUIRE((int)((data >> 16) & 0xFF) == 0);  // loop_length
    // Seed at Start time is whatever ProbLoopLinker singleton holds; do not
    // assert a specific value.
}

TEST_CASE("ProbabilityDivider PA4: round-trip preserves weights + loop_length + seed",
          "[per-applet-pilot][probabilitydivider]") {
    // Confirms singleton seed survives within a single .o instance.
    auto* loaded = setup_per_applet();

    // loop_length=8 triggers GenerateLoop on receive; seed=0xCAFE is stored
    // via ProbLoopLinker::SetSeed / GetSeed within this .o's singleton.
    set_pd_state(loaded->algorithm, pack_pd(15, 10, 5, 0, 8, 0xCAFE));
    uint64_t data = get_pd_state(loaded->algorithm);
    REQUIRE((int)((data >> 0)  & 0x0F) == 15);
    REQUIRE((int)((data >> 4)  & 0x0F) == 10);
    REQUIRE((int)((data >> 8)  & 0x0F) == 5);
    REQUIRE((int)((data >> 12) & 0x0F) == 0);
    REQUIRE((int)((data >> 16) & 0xFF) == 8);
    REQUIRE((int)((data >> 24) & 0xFFFF) == 0xCAFE);
}

TEST_CASE("ProbabilityDivider PA5: loop_length=0 skips GenerateLoop, seed still round-trips",
          "[per-applet-pilot][probabilitydivider]") {
    auto* loaded = setup_per_applet();

    set_pd_state(loaded->algorithm, pack_pd(8, 4, 2, 1, 0, 0xBEEF));
    uint64_t data = get_pd_state(loaded->algorithm);
    REQUIRE((int)((data >> 16) & 0xFF)   == 0);
    REQUIRE((int)((data >> 24) & 0xFFFF) == 0xBEEF);
    REQUIRE((int)((data >> 0)  & 0x0F)  == 8);
}

TEST_CASE("ProbabilityDivider PA6: all-zero weights: seed round-trips after step",
          "[per-applet-pilot][probabilitydivider]") {
    // Bus-level fire-count assertion dropped (10x rule + random draw).
    // State-injection only: inject weights=0, step several buffers, verify seed.
    auto* loaded = setup_per_applet();

    set_pd_state(loaded->algorithm, pack_pd(0, 0, 0, 0, 0, 0x1234));

    float* bus = nt::bus_frames_base();
    int    nBy4 = nt::bus_frame_count() / 4;
    for (int i = 0; i < 8; ++i) {
        loaded->factory->step(loaded->algorithm, bus, nBy4);
    }
    uint64_t data = get_pd_state(loaded->algorithm);
    REQUIRE((int)((data >> 24) & 0xFFFF) == 0x1234);
}

TEST_CASE("ProbabilityDivider PA7: serialise/deserialise JSON round-trip preserves state",
          "[per-applet-pilot][probabilitydivider]") {
    auto* loaded = setup_per_applet();

    set_pd_state(loaded->algorithm, pack_pd(7, 3, 1, 5, 16, 0xABCD));

    // Serialise to JSON.
    auto stream = nt::make_json_stream();
    stream->openObject();
    loaded->factory->serialise(loaded->algorithm, *stream);
    stream->closeObject();
    const std::string& json = stream->buffer();
    REQUIRE(!json.empty());

    // Reset state to zero and deserialise.
    set_pd_state(loaded->algorithm, 0);
    auto parse = nt::make_json_parse(json);
    bool ok = loaded->factory->deserialise(loaded->algorithm, *parse);
    REQUIRE(ok);

    uint64_t data = get_pd_state(loaded->algorithm);
    REQUIRE((int)((data >> 0)  & 0x0F)   == 7);
    REQUIRE((int)((data >> 4)  & 0x0F)   == 3);
    REQUIRE((int)((data >> 8)  & 0x0F)   == 1);
    REQUIRE((int)((data >> 12) & 0x0F)   == 5);
    REQUIRE((int)((data >> 16) & 0xFF)   == 16);
    REQUIRE((int)((data >> 24) & 0xFFFF) == 0xABCD);
}

TEST_CASE("ProbabilityDivider PA8: customUi encoder turn does not crash",
          "[per-applet-pilot][probabilitydivider]") {
    // Encoder routes through HemiPluginInterface::on_encoder_turn to vendor
    // OnEncoderMove. Not-in-edit-mode: encoder moves cursor, no value change.
    auto* loaded = setup_per_applet();

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);
}

TEST_CASE("ProbabilityDivider PA9: customUi encoder button press does not crash",
          "[per-applet-pilot][probabilitydivider]") {
    auto* loaded = setup_per_applet();

    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    // on_button_press is a no-op for ProbabilityDivider; confirm no crash.
}

TEST_CASE("ProbabilityDivider PA10: customUi aux button does not crash",
          "[per-applet-pilot][probabilitydivider]") {
    auto* loaded = setup_per_applet();

    _NT_uiData ui{};
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    // on_aux_button is a no-op; confirm no crash.
}
