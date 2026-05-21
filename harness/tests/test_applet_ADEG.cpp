// Per-applet test: ADEG (AD Envelope Generator).
//
// Manifest: shim/include/applet_manifests/ADEG.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ADEG.h
//
// 10x ticks-per-step concern: ADEG's Controller() advances phase state
// inside if (Clock(0)) and if (Clock(1)). A rising edge on gate input 0 or
// 1 sets clocked[ch]=true across all 10 inner ticks in one step buffer,
// so phase resets to 1 on each of those 10 ticks. After the gate clears and
// the bus is zeroed, subsequent steps run the envelope. Bus-level count
// assertions are not used here; coverage is via round-trip and steady-state
// output observation.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   Inputs (4):
//     v[0]  = input bus for "Trigger" (gate, default 1)
//     v[1]  = input bus for "Reverse" (gate, default 2)
//     v[2]  = input bus for "Attack"  (cv,   default 3)
//     v[3]  = input bus for "Decay"   (cv,   default 4)
//   Outputs (2):
//     v[4]  = output bus for "Out"    (default 13)
//     v[5]  = output mode for "Out"   (default 1 = replace)
//     v[6]  = output bus for "EOC"    (default 14)
//     v[7]  = output mode for "EOC"   (default 1 = replace)
//
// Default state: attack=50, decay=50.
// OnDataRequest packs: bits [0,8) = attack, bits [8,8) = decay.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/ADEG.cpp.
uint64_t adeg_applet_on_data_request(_NT_algorithm* self);
void     adeg_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices matching the default parameter layout.
static constexpr int kBusTrigger = 1;   // v[0] default gate input 0
static constexpr int kBusReverse = 2;   // v[1] default gate input 1
static constexpr int kBusOut     = 13;  // v[4] default cv output 0
static constexpr int kBusEOC     = 14;  // v[6] default gate output 1

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a gate high (5V) or low (0V) across all frames of a 1-based bus.
static void write_gate_bus(float* busFrames, int bus_1based, bool high) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    float v = high ? 5.0f : 0.0f;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = v;
}

// Read the last frame of an output bus as a float.
static float read_cv_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Return true if the last frame of the bus exceeds 0.5V.
static bool read_gate_bus_last(float* busFrames, int bus_1based) {
    return read_cv_bus_last(busFrames, bus_1based) > 0.5f;
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusTrigger, kBusReverse, kBusOut, kBusEOC}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("ADEG AD1: OnDataRequest packs attack=50 decay=50 after Start", "[per-applet][adeg]") {
    // Vendor Start() sets attack=50 and decay=50.
    // OnDataRequest packs attack into bits [0,8) and decay into bits [8,8).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = adeg_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 50u);          // attack at bits [0,8)
    REQUIRE(((packed >> 8) & 0xFF) == 50u);   // decay at bits [8,8)
}

TEST_CASE("ADEG AD2: serialise round-trip preserves attack and decay", "[per-applet][adeg]") {
    // Inject attack=200 decay=100 via OnDataReceive and confirm round-trip.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = (100u << 8) | 200u;  // decay=100 in bits [8,8), attack=200 in [0,8)
    adeg_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = adeg_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 200u);
    REQUIRE(((packed >> 8) & 0xFF) == 100u);
}

TEST_CASE("ADEG AD3: trigger gate starts envelope - output rises above zero", "[per-applet][adeg]") {
    // After a trigger on gate 0 the envelope enters attack phase.
    // With attack=50 the signal rises toward HEMISPHERE_MAX_CV.
    // Running a few steps after the trigger clears should show Out > 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Fire one trigger step.
    write_gate_bus(bus, kBusTrigger, true);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Clear gate and run several steps to let the attack advance.
    clear_buses(bus);
    for (int i = 0; i < 5; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    float out_v = read_cv_bus_last(bus, kBusOut);
    REQUIRE(out_v > 0.0f);
}

TEST_CASE("ADEG AD4: reverse gate swaps attack and decay roles", "[per-applet][adeg]") {
    // Set attack=0 (instant) and decay=200 (slow). Normal trigger: attack
    // phase is instant, decay starts immediately. Reverse trigger: attack
    // and decay are swapped, so effective_attack=200 (slow rise) and the
    // signal should remain near zero after a few steps.
    // Use OnDataReceive to inject attack=0, decay=200.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = (200u << 8) | 0u;  // decay=200, attack=0
    adeg_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Fire reverse trigger: effective_attack=decay=200 (slow), effective_decay=attack=0.
    write_gate_bus(bus, kBusReverse, true);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Clear gate. After just a few steps the envelope should be rising slowly.
    // With effective_attack=200 it will not have risen much above 0.
    clear_buses(bus);
    for (int i = 0; i < 3; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    // Signal should be well below the max CV (~5.99V in host units).
    float out_v = read_cv_bus_last(bus, kBusOut);
    REQUIRE(out_v < 5.0f);
}

TEST_CASE("ADEG AD5: encoder turn changes attack when cursor=0", "[per-applet][adeg]") {
    // Default cursor=0 (attack). Encoder turn +1 advances attack from 50 to 51.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE((adeg_applet_on_data_request(loaded->algorithm) & 0xFF) == 50u);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t packed = adeg_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 51u);
}

TEST_CASE("ADEG AD6: button press toggles cursor to decay, encoder changes decay", "[per-applet][adeg]") {
    // OnButtonPress flips cursor from 0 to 1. After that, encoder turn
    // changes decay (bits [8,8)) not attack (bits [0,8)).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Press button to move cursor to 1 (decay).
    _NT_uiData ui_btn{};
    ui_btn.controls    = kNT_encoderButtonL;
    ui_btn.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_btn);

    // Now turn encoder +1: decay should advance from 50 to 51.
    _NT_uiData ui_enc{};
    ui_enc.encoders[0] = 1;
    ui_enc.controls    = 0;
    ui_enc.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_enc);

    uint64_t packed = adeg_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 50u);           // attack unchanged
    REQUIRE(((packed >> 8) & 0xFF) == 51u);    // decay incremented
}

TEST_CASE("ADEG AD7: no trigger leaves output at zero", "[per-applet][adeg]") {
    // Without any trigger the envelope stays in phase=0 and Out stays 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    for (int i = 0; i < 10; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    // No trigger means phase=0 and the vendor sets no output; the bus
    // should read near zero (within floating-point rounding of the shim).
    REQUIRE(read_cv_bus_last(bus, kBusOut) < 0.1f);
}

TEST_CASE("ADEG AD8: draw does not crash", "[per-applet][adeg]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    bool result = loaded->factory->draw(loaded->algorithm);
    REQUIRE(result == true);
}
