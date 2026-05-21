// Per-applet test: Switch.
//
// Manifest: shim/include/applet_manifests/Switch.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Switch.h
//
// Switch provides a sequential CV switch (toggled by clock) and a gated CV
// switch (high/low gate selects between two CV inputs).
//
// OnDataRequest returns 0: no serialisable state. The test asserts this
// directly; no pack helper is needed.
//
// 10x clock multiplier concern: Switch toggles 'step' inside if(Clock(0)).
// With the default 10 inner ticks per step buffer, one rising clock edge
// causes step to flip 10 times (net: unchanged). Tests that verify the
// sequential toggle use hem_shim::inner_ticks_override=1 to get exactly
// one Controller() tick per step() call, making step advance by exactly 1.
//
// Bus parameter layout (emit_base_parameters, 4 inputs, 2 outputs):
//   v[0] = 1   Clock input bus   (gate, bus 1 default)
//   v[1] = 2   Gate  input bus   (gate, bus 2 default)
//   v[2] = 3   CV1   input bus   (cv,   bus 3 default)
//   v[3] = 4   CV2   input bus   (cv,   bus 4 default)
//   v[4] = 13  Toggled output bus (cv,  bus 13 default)
//   v[5] = 1   Toggled output mode
//   v[6] = 14  Gated output bus  (cv,  bus 14 default)
//   v[7] = 1   Gated output mode
//
// Custom step_impl maps:
//   bus param 0 (Clock) -> frame.clocked[0] / gate_high[0]
//   bus param 1 (Gate)  -> frame.clocked[1] / gate_high[1]
//   bus param 2 (CV1)   -> frame.inputs[0]  (vendor In(0))
//   bus param 3 (CV2)   -> frame.inputs[1]  (vendor In(1))

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <cstring>

// Test seam: Switch::OnDataRequest() always returns 0.
uint64_t switch_applet_on_data_request(_NT_algorithm* self);

// inner_ticks_override: set to 1 to run exactly one Controller() tick per
// step() call, making sequential-toggle tests deterministic.
namespace hem_shim { extern int inner_ticks_override; }

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Default bus indices matching v[] defaults from emit_base_parameters.
static constexpr int kBusClockIn   = 1;
static constexpr int kBusGateIn    = 2;
static constexpr int kBusCV1In     = 3;
static constexpr int kBusCV2In     = 4;
static constexpr int kBusToggledOut = 13;
static constexpr int kBusGatedOut   = 14;

// Write a constant CV voltage (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a single rising-edge gate pulse on a 1-based bus (frame 0 high, rest low).
static void write_gate_pulse(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    dst[0] = 6.0f;
    for (int i = 1; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Write a sustained gate high across all frames of a 1-based bus.
static void write_gate_high(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 6.0f;
}

// Clear a single bus to 0V.
static void clear_bus(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Read the CV output voltage from the last frame of a 1-based bus.
static float read_cv_output(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Reset runtime and HS::frame, then load the plugin.
static nt::LoadedPlugin* setup() {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    return loaded;
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

TEST_CASE("Switch SW1: plugin loads successfully", "[per-applet][switch]") {
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    REQUIRE(loaded->factory   != nullptr);
}

TEST_CASE("Switch SW2: OnDataRequest returns 0 (no serialisable state)", "[per-applet][switch]") {
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);
    REQUIRE(switch_applet_on_data_request(loaded->algorithm) == 0u);
}

TEST_CASE("Switch SW3: gated switch routes CV1 to Gated output when gate low", "[per-applet][switch]") {
    // When Gate bus is low, vendor routes In(0)=CV1 to Out(1)=Gated.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    write_cv_bus(bus, kBusCV1In,  2.0f);  // CV1 = 2V
    write_cv_bus(bus, kBusCV2In,  4.0f);  // CV2 = 4V
    clear_bus(bus, kBusGateIn);           // Gate low -> route CV1

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Out(1) should carry CV1 (~2V). Allow 0.1V tolerance for int rounding.
    float gated_out = read_cv_output(bus, kBusGatedOut);
    REQUIRE(gated_out > 1.9f);
    REQUIRE(gated_out < 2.1f);
}

TEST_CASE("Switch SW4: gated switch routes CV2 to Gated output when gate high", "[per-applet][switch]") {
    // When Gate bus is high, vendor routes In(1)=CV2 to Out(1)=Gated.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    write_cv_bus(bus, kBusCV1In,  2.0f);  // CV1 = 2V
    write_cv_bus(bus, kBusCV2In,  4.0f);  // CV2 = 4V
    write_gate_high(bus, kBusGateIn);     // Gate high -> route CV2

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float gated_out = read_cv_output(bus, kBusGatedOut);
    REQUIRE(gated_out > 3.9f);
    REQUIRE(gated_out < 4.1f);
}

TEST_CASE("Switch SW5: sequential switch initial step routes CV1 to Toggled output", "[per-applet][switch]") {
    // Start() sets step=0. Without any clock, Out(0) = In(step=0) = CV1.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    write_cv_bus(bus, kBusCV1In, 2.0f);  // CV1 = 2V
    write_cv_bus(bus, kBusCV2In, 4.0f);  // CV2 = 4V
    clear_bus(bus, kBusClockIn);         // No clock

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float toggled_out = read_cv_output(bus, kBusToggledOut);
    REQUIRE(toggled_out > 1.9f);
    REQUIRE(toggled_out < 2.1f);
}

TEST_CASE("Switch SW6: sequential switch advances step on clock rising edge", "[per-applet][switch]") {
    // With inner_ticks_override=1, step flips once per step() call.
    // After one clock pulse: step=1, Out(0) = In(1) = CV2.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    write_cv_bus(bus, kBusCV1In, 2.0f);
    write_cv_bus(bus, kBusCV2In, 4.0f);
    write_gate_pulse(bus, kBusClockIn);  // Rising edge on Clock

    hem_shim::inner_ticks_override = 1;  // Single tick -> step flips once
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float toggled_out = read_cv_output(bus, kBusToggledOut);
    REQUIRE(toggled_out > 3.9f);
    REQUIRE(toggled_out < 4.1f);
}

TEST_CASE("Switch SW7: sequential switch returns to CV1 on second clock pulse", "[per-applet][switch]") {
    // Two clock pulses (each in separate step() calls with 1 tick) should
    // return step to 0, routing CV1 to Toggled again.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    write_cv_bus(bus, kBusCV1In, 2.0f);
    write_cv_bus(bus, kBusCV2In, 4.0f);

    // First clock pulse: step -> 1
    write_gate_pulse(bus, kBusClockIn);
    hem_shim::inner_ticks_override = 1;
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Clear clock bus between steps to avoid re-triggering the edge detector.
    clear_bus(bus, kBusClockIn);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);  // no clock

    // Second clock pulse: step -> 0
    write_gate_pulse(bus, kBusClockIn);
    hem_shim::inner_ticks_override = 1;
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float toggled_out = read_cv_output(bus, kBusToggledOut);
    REQUIRE(toggled_out > 1.9f);
    REQUIRE(toggled_out < 2.1f);
}

TEST_CASE("Switch SW8: encoder and button presses do not crash", "[per-applet][switch]") {
    // Switch::OnEncoderMove and OnButtonPress are no-ops; routing them
    // must not crash.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE(true);
}
