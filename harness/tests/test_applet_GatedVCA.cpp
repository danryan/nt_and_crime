// Per-applet pilot test: GatedVCA.
//
// Manifest: shim/include/applet_manifests/GatedVCA.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/GatedVCA.h
//
// GatedVCA has no persistent state: OnDataRequest() always returns 0 and
// OnDataReceive() is a no-op. Tests verify that directly rather than via a
// round-trip pack helper.
//
// GatedVCA Controller() uses:
//   Gate(0): normally-off gate  - enables Out(0) when high
//   Gate(1): normally-on gate   - suppresses Out(1) when high
//   In(0):   signal input
//   In(1):   amplitude CV input
//
// Manifest input layout (populate_frame_from_bus splits by kind):
//   v[0]  = input bus for "Gate 1"  (gate_ch 0 -> clocked[0]/gate_high[0])
//   v[1]  = input bus for "Mute 2"  (gate_ch 1 -> clocked[1]/gate_high[1])
//   v[2]  = input bus for "Signal"  (cv_ch 0   -> inputs[0])
//   v[3]  = input bus for "Amp"     (cv_ch 1   -> inputs[1])
//   v[4]  = output bus for "Closed" (default 13)
//   v[5]  = output mode for "Closed"(default 1 = replace)
//   v[6]  = output bus for "Open"   (default 14)
//   v[7]  = output mode for "Open"  (default 1 = replace)
//
// 10x inner-tick note: Controller() runs 10 times per step(). Gate(ch)
// reads gate_high[ch] which stays asserted for the full step after one
// rising edge. Assertions use bus-level output values at end of step.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/GatedVCA.cpp.
extern "C" uint64_t gatedvca_applet_on_data_request(_NT_algorithm* self);
extern "C" void     gatedvca_applet_encoder_move(_NT_algorithm* self, int dir);

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Bus indices matching the manifest's default parameter layout.
static constexpr int kBusGate1  = 1;   // v[0] default
static constexpr int kBusMute2  = 2;   // v[1] default
static constexpr int kBusSignal = 3;   // v[2] default
static constexpr int kBusAmp    = 4;   // v[3] default
static constexpr int kBusClosed = 13;  // v[4] default
static constexpr int kBusOpen   = 14;  // v[6] default

static void write_cv_bus(float* busFrames, int bus_1based, float v) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = v;
}

static float read_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_buses(float* busFrames) {
    for (int bus : {kBusGate1, kBusMute2, kBusSignal, kBusAmp, kBusClosed, kBusOpen}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("GatedVCA GV1: OnDataRequest returns 0 after Start", "[per-applet][gatedvca]") {
    // GatedVCA has no persistent state; OnDataRequest() always returns 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    REQUIRE(gatedvca_applet_on_data_request(loaded->algorithm) == 0u);
}

TEST_CASE("GatedVCA GV2: OnDataRequest returns 0 after encoder move", "[per-applet][gatedvca]") {
    // Encoder adjusts amp_offset_pct but OnDataRequest still returns 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    gatedvca_applet_encoder_move(loaded->algorithm, 10);

    REQUIRE(gatedvca_applet_on_data_request(loaded->algorithm) == 0u);
}

TEST_CASE("GatedVCA GV3: step with no inputs completes cleanly", "[per-applet][gatedvca]") {
    // Quiescent step: no gate inputs, no signal. Step must complete without fault.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(true);
}

TEST_CASE("GatedVCA GV4: gate high on Gate 1 passes signal to Closed output", "[per-applet][gatedvca]") {
    // When Gate(0) is high, Out(0) = Proportion(amplitude, MAX, signal).
    // With signal=MAX and amp=MAX, output should be non-zero (positive).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Gate 1 high: fill bus with 5V
    write_cv_bus(bus, kBusGate1, 5.0f);
    // Signal at full scale: HEMISPHERE_MAX_INPUT_CV in hem units = ~7677 -> 5V on bus
    write_cv_bus(bus, kBusSignal, 5.0f);
    // Amp at full scale
    write_cv_bus(bus, kBusAmp, 5.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Closed output should receive a non-zero value (gate passes signal).
    float closed_out = read_bus_last(bus, kBusClosed);
    REQUIRE(closed_out != 0.0f);
}

TEST_CASE("GatedVCA GV5: gate low on Gate 1 silences Closed output", "[per-applet][gatedvca]") {
    // When Gate(0) is low, Out(0) = 0 regardless of signal.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Gate 1 low (default 0)
    write_cv_bus(bus, kBusSignal, 5.0f);
    write_cv_bus(bus, kBusAmp, 5.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float closed_out = read_bus_last(bus, kBusClosed);
    REQUIRE(closed_out == 0.0f);
}

TEST_CASE("GatedVCA GV6: gate low on Mute 2 passes signal to Open output", "[per-applet][gatedvca]") {
    // When Gate(1) is low, Out(1) = Proportion(amplitude, MAX, signal) - normally on.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Mute 2 gate low (default), signal and amp at full scale.
    write_cv_bus(bus, kBusSignal, 5.0f);
    write_cv_bus(bus, kBusAmp, 5.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Open output should be non-zero (gate is low = normally on).
    float open_out = read_bus_last(bus, kBusOpen);
    REQUIRE(open_out != 0.0f);
}

TEST_CASE("GatedVCA GV7: gate high on Mute 2 silences Open output", "[per-applet][gatedvca]") {
    // When Gate(1) is high, Out(1) = 0 (muted).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Mute 2 gate high
    write_cv_bus(bus, kBusMute2, 5.0f);
    write_cv_bus(bus, kBusSignal, 5.0f);
    write_cv_bus(bus, kBusAmp, 5.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float open_out = read_bus_last(bus, kBusOpen);
    REQUIRE(open_out == 0.0f);
}

TEST_CASE("GatedVCA GV8: encoder turn adjusts amp offset without crash", "[per-applet][gatedvca]") {
    // Encoder move changes amp_offset_cv; verify no fault.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE(gatedvca_applet_on_data_request(loaded->algorithm) == 0u);
}

TEST_CASE("GatedVCA GV9: encoder button press via customUi does not crash", "[per-applet][gatedvca]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
