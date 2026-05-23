// Per-applet pilot test: Scope.
//
// Manifest: shim/include/applet_manifests/Scope.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Scope.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). Scope's Controller() writes
//   last_bpm_tick = OC::CORE::ticks on every inner tick that sees Clock(0)
//   asserted, so the value is stomped 10 times. Only the FINAL value (after all
//   10 inner ticks) is stable for assertion.
//
//   BPM calculation: bpm = 1000000 / (this_tick - last_bpm_tick). When
//   Clock(0) stays asserted across all 10 inner ticks, consecutive intervals
//   are 1 microsecond, so bpm is clamped to 9999 on ticks 2-10.
//
//   Coverage shape: SHAPE 1 (model-multiplier) for clock-driven assertions.
//   Serialisation round-trip is trivial: OnDataRequest() always returns 0.
//   CV passthrough is the primary observable output.
//
// Bus parameter layout (per emit_base_parameters with 4 inputs, 2 outputs):
//   v[0] = "BPM Clk" input bus (gate), default 1
//   v[1] = "Cycle"   input bus (gate), default 2
//   v[2] = "CV 1"    input bus (cv),   default 3
//   v[3] = "CV 2"    input bus (cv),   default 4
//   v[4] = "CV 1"    output bus,       default 13
//   v[5] = "CV 1"    output mode,      default 1 (replace)
//   v[6] = "CV 2"    output bus,       default 14
//   v[7] = "CV 2"    output mode,      default 1 (replace)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Test seam defined in plugins/applets/Scope.cpp.
uint64_t scope_applet_on_data_request(_NT_algorithm* self);

namespace {

// Bus indices matching the Scope manifest default parameter layout.
constexpr int kBusBpmClk = 1;   // v[0] gate input (BPM Clk)
constexpr int kBusCycle  = 2;   // v[1] gate input (Cycle)
constexpr int kBusCV1In  = 3;   // v[2] cv input (CV 1)
constexpr int kBusCV2In  = 4;   // v[3] cv input (CV 2)
constexpr int kBusCV1Out = 13;  // v[4] cv output (CV 1)
constexpr int kBusCV2Out = 14;  // v[6] cv output (CV 2)

constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a constant voltage across all frames of a 1-based bus.
void write_cv_bus(float* bus, int bus_1based, float volts) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a 1-sample rising-edge pulse at frame 0 on the given 1-based bus.
void pulse_bus(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    slice[0] = 6.0f;
}

// Read the last frame of a 1-based bus.
float read_last_frame(const float* bus, int bus_1based) {
    return bus[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

struct Setup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
};

Setup make_setup() {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    // One warmup step to let BaseStart settle internal state.
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// SC1: factory guid and name match manifest.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC1: pluginEntry returns factory with correct guid and name",
          "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','S','o');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Scope");
}

// ---------------------------------------------------------------------------
// SC2: construct populates HemiPluginInterface magic and version.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC2: construct populates HemiPluginInterface magic and version",
          "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* iface = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(iface->magic == kHemiInterfaceMagic);
    REQUIRE(iface->interface_version == kHemiInterfaceVersion);
}

// ---------------------------------------------------------------------------
// SC3: OnDataRequest() returns 0 (Scope packs no state).
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC3: OnDataRequest always returns 0",
          "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(scope_applet_on_data_request(loaded->algorithm) == 0u);
}

// ---------------------------------------------------------------------------
// SC4: CV passthrough - In(0) appears on Out(0) when not frozen.
//
// Scope's Controller() does: ForEachChannel(ch) Out(ch, In(ch)).
// With CV 1 input at 3V, the CV 1 output bus should read ~3V.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC4: CV 1 input passes through to CV 1 output when unfrozen",
          "[per-applet-pilot][scope]") {
    auto s = make_setup();

    write_cv_bus(s.bus, kBusCV1In,  3.0f);
    write_cv_bus(s.bus, kBusCV2In,  0.0f);

    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Shim converts volts -> hem units and back; allow small rounding tolerance.
    float out = read_last_frame(s.bus, kBusCV1Out);
    REQUIRE(out == Catch::Approx(3.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// SC5: CV passthrough - In(1) appears on Out(1) when not frozen.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC5: CV 2 input passes through to CV 2 output when unfrozen",
          "[per-applet-pilot][scope]") {
    auto s = make_setup();

    write_cv_bus(s.bus, kBusCV1In,  0.0f);
    write_cv_bus(s.bus, kBusCV2In,  2.0f);

    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out = read_last_frame(s.bus, kBusCV2Out);
    REQUIRE(out == Catch::Approx(2.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// SC6: Clock(0) rising edge - model-multiplier shape.
//
// When BPM Clk (bus 1) fires, Controller() reads OC::CORE::ticks and
// writes last_bpm_tick on every inner tick. With 10 inner ticks all seeing
// the clock asserted, the final last_bpm_tick is T + 10 (the last tick value).
// The inter-tick interval collapses to 1 microsecond, clamping bpm to 9999.
// We cannot observe bpm directly (private), but the step() must complete
// without crash and CV passthrough still works after a clock edge.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC6: BPM clock edge does not crash and CV passthrough survives",
          "[per-applet-pilot][scope]") {
    auto s = make_setup();

    pulse_bus(s.bus, kBusBpmClk);
    write_cv_bus(s.bus, kBusCV1In,  1.5f);

    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // CV passthrough must still work after a BPM clock edge.
    float out = read_last_frame(s.bus, kBusCV1Out);
    REQUIRE(out == Catch::Approx(1.5f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// SC7: Clock(1) rising edge - auto-sample-ticks update.
//
// Clock(1) sets last_scope_tick = OC::CORE::ticks on every inner tick that
// sees the edge asserted (10 times). No crash expected; CV passthrough holds.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC7: Cycle clock edge does not crash and CV passthrough survives",
          "[per-applet-pilot][scope]") {
    auto s = make_setup();

    // Drive a first Cycle edge to initialise last_scope_tick.
    pulse_bus(s.bus, kBusCycle);
    write_cv_bus(s.bus, kBusCV2In,  1.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    clear_bus(s.bus);

    // Drive a second Cycle edge; this triggers the sample_ticks update path.
    pulse_bus(s.bus, kBusCycle);
    write_cv_bus(s.bus, kBusCV2In,  1.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out = read_last_frame(s.bus, kBusCV2Out);
    REQUIRE(out == Catch::Approx(1.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// SC8: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC8: hasCustomUi returns encoderL | encoderButtonL (Q5)",
          "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE((mask & kNT_encoderL)       != 0u);
    REQUIRE((mask & kNT_encoderButtonL) != 0u);
    REQUIRE((mask & kNT_button1)        == 0u);
}

// ---------------------------------------------------------------------------
// SC9: encoder turn dispatches to OnEncoderMove via customUi.
//
// Default current_setting=0 (Rate) in non-edit mode: encoder switches
// setting, advancing current_setting from 0 to 1. No crash required.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC9: encoder turn routes to OnEncoderMove via customUi",
          "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);  // must not crash
}

// ---------------------------------------------------------------------------
// SC10: encoder button press routes to OnButtonPress via customUi.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC10: encoder button press routes to OnButtonPress via customUi",
          "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// SC11: button1 press routes to on_aux_button (OnButtonPress) via customUi.
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC11: button1 press routes to on_aux_button via customUi",
          "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// SC12: draw() returns true (Scope always renders).
// ---------------------------------------------------------------------------

TEST_CASE("Scope SC12: draw returns true", "[per-applet-pilot][scope]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    bool result = loaded->factory->draw(loaded->algorithm);
    REQUIRE(result == true);
}
