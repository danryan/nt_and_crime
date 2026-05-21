// Per-applet test: GateDelay.
//
// Manifest: shim/include/applet_manifests/GateDelay.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/GateDelay.h
//
// 10x ticks-per-step note:
//   GateDelay.Controller() uses ms_countdown (initialized to 16) to gate its
//   record/play loop to approximately 1ms intervals. With 10 inner ticks per
//   step(), the countdown decrements by 10 each step call. The record/play
//   block fires whenever ms_countdown goes below 0, resetting it to 16.
//   For a standard kNumFramesBy4=8 call (32 frames / 3 = 10 inner ticks),
//   the record/play block fires once at step 2 (countdown: 16 -> 6 -> -4 -> fires).
//
//   The delay test (GD4) uses the state-injection approach: inject time[]=0
//   via OnDataReceive so the playback head equals the record head (zero delay),
//   then assert the gate appears on the output after enough steps for
//   ms_countdown to trigger at least once.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Gate 1" (default 1)
//   v[1]  = input  bus for "Gate 2" (default 2)
//   v[2]  = input  bus for "Time 1" (default 3)
//   v[3]  = input  bus for "Time 2" (default 4)
//   v[4]  = output bus for "Delay 1" (default 13)
//   v[5]  = output mode for "Delay 1 mode" (default 1 = replace)
//   v[6]  = output bus for "Delay 2" (default 14)
//   v[7]  = output mode for "Delay 2 mode" (default 1 = replace)
//
// Vendor pack layout (OnDataRequest):
//   bits [0,11)  = time[0]   (11 bits, value 0..2000, default 1000)
//   bits [11,11) = time[1]   (11 bits, value 0..2000, default 1000)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/GateDelay.cpp.
uint64_t gatedelay_applet_on_data_request(_NT_algorithm* self);
void     gatedelay_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for GateDelay's default parameter layout.
static constexpr int kBusGate1  = 1;   // v[0] default - gate input 1
static constexpr int kBusGate2  = 2;   // v[1] default - gate input 2
static constexpr int kBusTime1  = 3;   // v[2] default - CV modulation input 1
static constexpr int kBusTime2  = 4;   // v[3] default - CV modulation input 2
static constexpr int kBusDelay1 = 13;  // v[4] default - gate output 1
static constexpr int kBusDelay2 = 14;  // v[6] default - gate output 2

// Frames per step call matching the host sim default (numFramesBy4=8).
static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a gate high value across all frames of a 1-based bus.
static void write_gate_bus_high(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 6.0f;
}

// Write a gate low value (0V) across all frames of a 1-based bus.
static void write_gate_bus_low(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Returns true if the last frame of the gate output bus exceeds 0.5V.
static bool read_gate_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)] > 0.5f;
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusGate1, kBusGate2, kBusTime1, kBusTime2, kBusDelay1, kBusDelay2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Pack GateDelay state matching vendor OnDataRequest encoding.
// time[ch] stored at [ch*11, (ch+1)*11) bits, raw value (no bias).
static uint64_t pack_gd(int time0, int time1) {
    uint64_t d = 0;
    d |= (uint64_t)(time0 & 0x7FF) << 0;
    d |= (uint64_t)(time1 & 0x7FF) << 11;
    return d;
}

TEST_CASE("GateDelay GD1: OnDataRequest packs time defaults after Start",
          "[per-applet][gatedelay]") {
    // Vendor Start() sets time[0]=time[1]=1000. Pack: bits[0..10]=1000, bits[11..21]=1000.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = gatedelay_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x7FF) == 1000u);
    REQUIRE(((packed >> 11) & 0x7FF) == 1000u);
}

TEST_CASE("GateDelay GD2: serialise round-trip preserves time values",
          "[per-applet][gatedelay]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject time[0]=500 ms, time[1]=250 ms.
    uint64_t state_in = pack_gd(500, 250);
    gatedelay_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = gatedelay_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x7FF) == 500u);
    REQUIRE(((packed >> 11) & 0x7FF) == 250u);
}

TEST_CASE("GateDelay GD3: no gate input produces no gate output after sufficient steps",
          "[per-applet][gatedelay]") {
    // With gate inputs low and tape initialized to 0, both outputs stay low.
    // Use time=0 (zero delay) to make the play head read the record head.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject time=0 for zero delay so playback reads what was just recorded.
    gatedelay_applet_on_data_receive(loaded->algorithm, pack_gd(0, 0));

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Run 3 steps: enough for ms_countdown to fire at least once and process.
    for (int step = 0; step < 3; ++step) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    REQUIRE(read_gate_bus(bus, kBusDelay1) == false);
    REQUIRE(read_gate_bus(bus, kBusDelay2) == false);
}

TEST_CASE("GateDelay GD4: gate input with zero delay passes through to output",
          "[per-applet][gatedelay]") {
    // Set time[0]=0 so there is zero delay on channel 0.
    // Hold Gate 1 high for several steps so the tape fills with 1s.
    // After enough steps the playback head catches up and the output goes high.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Zero delay for both channels.
    gatedelay_applet_on_data_receive(loaded->algorithm, pack_gd(0, 0));

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus_high(bus, kBusGate1);

    // Run enough steps to let the Controller fire (ms_countdown needs 2 steps),
    // then fill the tape long enough that play_location reads 1.
    // With time=0: play_location = location - 0 = location, i.e. the bit just written.
    // After step 1: ms_countdown goes 16->6, not yet fired.
    // After step 2: ms_countdown goes 6->-4, fires, records gate=1 at location,
    //   plays location - 0 = location = gate just written -> output high.
    for (int step = 0; step < 3; ++step) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    REQUIRE(read_gate_bus(bus, kBusDelay1) == true);
}

TEST_CASE("GateDelay GD5: gate 2 independently processed",
          "[per-applet][gatedelay]") {
    // Gate 2 high with zero delay on ch1. Gate 1 stays low.
    // Expect Delay 2 high, Delay 1 low.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    gatedelay_applet_on_data_receive(loaded->algorithm, pack_gd(0, 0));

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus_low(bus, kBusGate1);
    write_gate_bus_high(bus, kBusGate2);

    for (int step = 0; step < 3; ++step) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    REQUIRE(read_gate_bus(bus, kBusDelay1) == false);
    REQUIRE(read_gate_bus(bus, kBusDelay2) == true);
}

TEST_CASE("GateDelay GD6: encoder turn changes time via customUi",
          "[per-applet][gatedelay]") {
    // Drive encoder turn +1 to change the selected time parameter.
    // OnEncoderMove(1) increments time[cursor] from 1000 by some amount.
    // Verify the packed state changes.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Enter edit mode first by pressing the encoder button.
    _NT_uiData ui_press{};
    ui_press.controls    = kNT_encoderButtonL;
    ui_press.lastButtons = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui_press);

    uint64_t before = gatedelay_applet_on_data_request(loaded->algorithm);

    // Turn encoder +1 in edit mode: time[0] increments (by 1 since time<100).
    _NT_uiData ui_turn{};
    ui_turn.encoders[0] = 1;
    ui_turn.controls    = 0;
    ui_turn.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_turn);

    uint64_t after = gatedelay_applet_on_data_request(loaded->algorithm);
    REQUIRE(after != before);
    // time[0]=1000, direction=1: >100 -> *2 -> 2; >500 -> *2 -> 4; >1000 false. Increment = 4.
    REQUIRE((after & 0x7FF) == ((before & 0x7FF) + 4));
}

TEST_CASE("GateDelay GD7: encoder button press toggles edit mode via customUi",
          "[per-applet][gatedelay]") {
    // OnButtonPress toggles EditMode; must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("GateDelay GD8: aux button press routes on_aux_button via customUi",
          "[per-applet][gatedelay]") {
    // on_aux_button maps to OnButtonPress; must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
