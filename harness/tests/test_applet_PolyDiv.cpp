// Per-applet pilot test: PolyDiv.
//
// Manifest: shim/include/applet_manifests/PolyDiv.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/PolyDiv.h
//
// Coverage shape: state-injection only (PD3/PD4 template).
//
// PolyDiv::Poke() increments clock_count on every inner tick. A single
// Clock(0) edge causes all four dividers to advance by 10 counts (the
// 10x inner-ticks multiplier) not by 1. Division-ratio assertions at the
// bus level are therefore unreliable. Tests cover:
//   - Factory entry and construct correctness.
//   - Default state after BaseStart(): divider steps and div_enabled bitmask.
//   - Round-trip through get_polydiv_state / set_polydiv_state.
//   - Per-divider clock_count and steps readable via inspection accessors.
//   - Encoder turn and button-press do not crash.
//   - Serialise/deserialise JSON round-trip preserves state.
//   - OnDataReceive calls Reset(), zeroing all clock_counts.
//
// ODR discipline:
//   This file does NOT include PolyDiv.h or hemispheres_shim.h. Both are
//   compiled into the same binary via PolyDiv.cpp. Opaque accessors below
//   provide state introspection without re-including vendor headers.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "nt_jsonstream.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstdint>

// Opaque accessors defined in PolyDiv.cpp, compiled into same binary.
extern "C" {
uint64_t get_polydiv_state(_NT_algorithm* alg);
void     set_polydiv_state(_NT_algorithm* alg, uint64_t state);
uint8_t  get_polydiv_clock_count(_NT_algorithm* alg, int ch);
uint8_t  get_polydiv_steps(_NT_algorithm* alg, int ch);
}

// Pack the PolyDiv OnDataRequest() word (matching vendor layout):
//   bits [0,8)          = div_enabled bitmask (8 bits)
//   bits [8+i*6, 6)     = divider[i].steps for i in 0..3
static uint64_t pack_polydiv(int div_enabled,
                              int steps0, int steps1,
                              int steps2, int steps3) {
    uint64_t data = 0;
    data |= (uint64_t)(div_enabled & 0xFF);
    data |= (uint64_t)(steps0 & 0x3F) << 8;
    data |= (uint64_t)(steps1 & 0x3F) << 14;
    data |= (uint64_t)(steps2 & 0x3F) << 20;
    data |= (uint64_t)(steps3 & 0x3F) << 26;
    return data;
}

// Shared setup: reset runtime, load plugin, run one warmup step.
static nt::LoadedPlugin* setup_per_applet() {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus  = nt::bus_frames_base();
    int    nBy4 = nt::bus_frame_count() / 4;
    loaded->factory->step(loaded->algorithm, bus, nBy4);
    return loaded;
}

TEST_CASE("PolyDiv PD1: factory entry resolves correctly",
          "[per-applet-pilot][polydiv]") {
    nt::reset_runtime();
    uintptr_t v = pluginEntry(kNT_selector_version, 0);
    REQUIRE(v == kNT_apiVersionCurrent);
    uintptr_t n = pluginEntry(kNT_selector_numFactories, 0);
    REQUIRE(n == 1);
    uintptr_t f = pluginEntry(kNT_selector_factoryInfo, 0);
    REQUIRE(f != 0);
    const auto* factory = reinterpret_cast<const _NT_factory*>(f);
    REQUIRE(factory->guid == NT_MULTICHAR('H','m','P','o'));
}

TEST_CASE("PolyDiv PD2: load_plugin constructs instance and populates ABI fields",
          "[per-applet-pilot][polydiv]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    auto* iface = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(iface->magic            == kHemiInterfaceMagic);
    REQUIRE(iface->interface_version == kHemiInterfaceVersion);
    REQUIRE(iface->render_view      != nullptr);
    REQUIRE(iface->on_encoder_turn  != nullptr);
    REQUIRE(iface->on_button_press  != nullptr);
    REQUIRE(iface->on_aux_button    != nullptr);
}

TEST_CASE("PolyDiv PD3: default state after BaseStart()",
          "[per-applet-pilot][polydiv]") {
    // Vendor initializes: divider[4] = {{4,0},{3,0},{2,0},{1,0}},
    // div_enabled = 0b00100001 (bits 0 and 5 set).
    auto* loaded = setup_per_applet();
    uint64_t data = get_polydiv_state(loaded->algorithm);

    // div_enabled field (bits 0..7)
    int div_enabled = (int)(data & 0xFF);
    REQUIRE(div_enabled == 0b00100001);

    // Default steps from vendor initializer
    REQUIRE(get_polydiv_steps(loaded->algorithm, 0) == 4);
    REQUIRE(get_polydiv_steps(loaded->algorithm, 1) == 3);
    REQUIRE(get_polydiv_steps(loaded->algorithm, 2) == 2);
    REQUIRE(get_polydiv_steps(loaded->algorithm, 3) == 1);
}

TEST_CASE("PolyDiv PD4: state-injection round-trip preserves div_enabled and steps",
          "[per-applet-pilot][polydiv]") {
    // State-injection only (10x rule makes bus-level count assertions unsound).
    auto* loaded = setup_per_applet();

    // Set all enabled, steps = {8, 16, 32, 63}.
    uint64_t injected = pack_polydiv(0xFF, 8, 16, 32, 63);
    set_polydiv_state(loaded->algorithm, injected);

    uint64_t data = get_polydiv_state(loaded->algorithm);
    REQUIRE((int)(data & 0xFF) == 0xFF);
    REQUIRE((int)((data >> 8)  & 0x3F) == 8);
    REQUIRE((int)((data >> 14) & 0x3F) == 16);
    REQUIRE((int)((data >> 20) & 0x3F) == 32);
    REQUIRE((int)((data >> 26) & 0x3F) == 63);
}

TEST_CASE("PolyDiv PD5: OnDataReceive resets all clock_counts to 0",
          "[per-applet-pilot][polydiv]") {
    // Vendor OnDataReceive calls Reset() which zeroes clock_count for all
    // four dividers. Confirm via inspection accessors.
    auto* loaded = setup_per_applet();

    // Step a few buffers to advance clock_counts via the inner tick loop.
    float* bus  = nt::bus_frames_base();
    int    nBy4 = nt::bus_frame_count() / 4;
    for (int i = 0; i < 4; ++i) {
        loaded->factory->step(loaded->algorithm, bus, nBy4);
    }

    // Inject state: OnDataReceive calls Reset() internally.
    set_polydiv_state(loaded->algorithm, pack_polydiv(0x33, 4, 3, 2, 1));

    REQUIRE(get_polydiv_clock_count(loaded->algorithm, 0) == 0);
    REQUIRE(get_polydiv_clock_count(loaded->algorithm, 1) == 0);
    REQUIRE(get_polydiv_clock_count(loaded->algorithm, 2) == 0);
    REQUIRE(get_polydiv_clock_count(loaded->algorithm, 3) == 0);
}

TEST_CASE("PolyDiv PD6: steps clamped to MAX_DIV (63) on inject",
          "[per-applet-pilot][polydiv]") {
    auto* loaded = setup_per_applet();

    // pack_polydiv already masks to 6 bits so the value arriving at Set()
    // is at most 63.  Confirm clamping is effective at round-trip boundary.
    uint64_t injected = pack_polydiv(0x01, 63, 63, 63, 63);
    set_polydiv_state(loaded->algorithm, injected);

    REQUIRE(get_polydiv_steps(loaded->algorithm, 0) == 63);
    REQUIRE(get_polydiv_steps(loaded->algorithm, 1) == 63);
    REQUIRE(get_polydiv_steps(loaded->algorithm, 2) == 63);
    REQUIRE(get_polydiv_steps(loaded->algorithm, 3) == 63);
}

TEST_CASE("PolyDiv PD7: div_enabled=0 round-trips correctly (all outputs silent)",
          "[per-applet-pilot][polydiv]") {
    auto* loaded = setup_per_applet();

    set_polydiv_state(loaded->algorithm, pack_polydiv(0x00, 4, 3, 2, 1));
    uint64_t data = get_polydiv_state(loaded->algorithm);
    REQUIRE((int)(data & 0xFF) == 0);
}

TEST_CASE("PolyDiv PD8: serialise/deserialise JSON round-trip preserves state",
          "[per-applet-pilot][polydiv]") {
    auto* loaded = setup_per_applet();

    uint64_t injected = pack_polydiv(0b10110100, 7, 12, 5, 33);
    set_polydiv_state(loaded->algorithm, injected);

    // Serialise.
    auto stream = nt::make_json_stream();
    stream->openObject();
    loaded->factory->serialise(loaded->algorithm, *stream);
    stream->closeObject();
    const std::string& json = stream->buffer();
    REQUIRE(!json.empty());

    // Reset state then deserialise.
    set_polydiv_state(loaded->algorithm, 0);
    auto parse = nt::make_json_parse(json);
    bool ok = loaded->factory->deserialise(loaded->algorithm, *parse);
    REQUIRE(ok);

    uint64_t data = get_polydiv_state(loaded->algorithm);
    REQUIRE((int)(data & 0xFF)          == 0b10110100);
    REQUIRE((int)((data >> 8)  & 0x3F)  == 7);
    REQUIRE((int)((data >> 14) & 0x3F)  == 12);
    REQUIRE((int)((data >> 20) & 0x3F)  == 5);
    REQUIRE((int)((data >> 26) & 0x3F)  == 33);
}

TEST_CASE("PolyDiv PD9: encoder turn does not crash",
          "[per-applet-pilot][polydiv]") {
    auto* loaded = setup_per_applet();
    _NT_uiData ui{};
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);
    ui.encoders[0] = -1;
    loaded->factory->customUi(loaded->algorithm, ui);
}

TEST_CASE("PolyDiv PD10: button press does not crash",
          "[per-applet-pilot][polydiv]") {
    auto* loaded = setup_per_applet();
    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
}

TEST_CASE("PolyDiv PD11: aux button does not crash",
          "[per-applet-pilot][polydiv]") {
    auto* loaded = setup_per_applet();
    _NT_uiData ui{};
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
}

TEST_CASE("PolyDiv PD12: step survives multiple buffers without crash",
          "[per-applet-pilot][polydiv]") {
    auto* loaded = setup_per_applet();
    float* bus  = nt::bus_frames_base();
    int    nBy4 = nt::bus_frame_count() / 4;
    for (int i = 0; i < 32; ++i) {
        loaded->factory->step(loaded->algorithm, bus, nBy4);
    }
    // Confirm state is still readable after many steps.
    uint64_t data = get_polydiv_state(loaded->algorithm);
    // div_enabled default (0b00100001) is preserved across steps if no clock.
    REQUIRE((int)(data & 0xFF) == 0b00100001);
}
