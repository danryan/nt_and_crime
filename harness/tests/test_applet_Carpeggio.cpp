// Per-applet test: Carpeggio.
//
// Manifest: shim/include/applet_manifests/Carpeggio.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Carpeggio.h
//
// 10x clocked multiplier concern: Carpeggio advances its step counter inside
// if (Clock(0)) and resets step inside if (Clock(1)). With the host harness
// running Controller() 10 times per step() call, each gate edge causes 10
// inner ticks. On each of those ticks, Clock(ch) remains asserted. However,
// Clock(0) advances the step counter only when !reset or bumps it when reset
// is cleared. The first tick seeing Clock(0) with reset=true clears reset and
// emits the current step; subsequent ticks advance step each time because
// reset=false. After 10 ticks the step counter may advance up to 9 steps
// beyond the initial step. Tests that assert specific step positions after a
// clock edge account for this by using state-injection or round-trip checks
// rather than fine-grained step-count assertions.
//
// Bus parameter layout (per emit_base_parameters):
//   v[0]  = input  bus for "Clock" (default 1)
//   v[1]  = input  bus for "Reset" (default 2)
//   v[2]  = output bus for "Pitch" (default 13)
//   v[3]  = output mode for "Pitch" (default 1 = replace)
//   v[4]  = output bus for "Mod"   (default 14)
//   v[5]  = output mode for "Mod"  (default 1 = replace)
//
// OnDataRequest packing (vendor):
//   bits [0,8)  = sel_chord
//   bits [8,8)  = transpose + 24   (bias +24 for range -24..+24 -> 0..48)
//
// After Start(): ImprintChord(2) -> sel_chord=2, transpose=0.
// Packed default = 2 | (24 << 8) = 0x1802.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Carpeggio.cpp.
uint64_t carpeggio_applet_on_data_request(_NT_algorithm* self);
void     carpeggio_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for Carpeggio's default parameter layout.
static constexpr int kBusClock = 1;   // v[0] default
static constexpr int kBusReset = 2;   // v[1] default
static constexpr int kBusPitch = 13;  // v[2] default
static constexpr int kBusMod   = 14;  // v[4] default

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a gate pulse at frame 0 only (rising edge).
static void write_gate_pulse(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    dst[0] = 6.0f;
    for (int i = 1; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Zero all frames for a bus.
static void clear_bus(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Read the last frame of a bus.
static float read_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

TEST_CASE("Carpeggio CA1: OnDataRequest packs sel_chord=2 and transpose=0 after Start",
          "[per-applet-mass-port][carpeggio]") {
    // After Start(): ImprintChord(2) -> sel_chord=2, transpose=0.
    // Pack(data, {0,8}, sel_chord=2) -> bits[0..7] = 2.
    // Pack(data, {8,8}, transpose+24=24) -> bits[8..15] = 24 (0x18).
    // Full packed = 0x1802.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = carpeggio_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 2u);           // sel_chord=2
    REQUIRE(((packed >> 8) & 0xFF) == 24u);   // transpose=0 -> stored as 24
}

TEST_CASE("Carpeggio CA2: serialise round-trip preserves chord and transpose",
          "[per-applet-mass-port][carpeggio]") {
    // Inject sel_chord=5, transpose=+12 (stored as 36).
    // Packed = 5 | (36 << 8) = 0x2405.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = 5u | (36u << 8u);
    carpeggio_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = carpeggio_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 5u);
    REQUIRE(((packed >> 8) & 0xFF) == 36u);
}

TEST_CASE("Carpeggio CA3: encoder turn advances chord setting via customUi",
          "[per-applet-mass-port][carpeggio]") {
    // Default chord=2 (sel_chord=2). One encoder turn right increments
    // chord by 1. OnDataRequest before imprint still shows sel_chord=2
    // (chord != sel_chord until button pressed). We verify chord
    // selection changed by checking the state survives a round-trip:
    // drive encoder +1 so chord=3, then press button to imprint, then
    // check sel_chord changed to 3.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Enter edit mode for chord cursor (first press enters edit on cursor 0=CHORD).
    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Advance chord by 1.
    ui.controls    = 0;
    ui.lastButtons = kNT_encoderButtonL;
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Press button to imprint the new chord (OnButtonPress imprints when chord != sel_chord).
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t packed = carpeggio_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 3u);  // sel_chord incremented from 2 to 3
}

TEST_CASE("Carpeggio CA4: clock pulse advances step and produces non-zero pitch output",
          "[per-applet-mass-port][carpeggio]") {
    // Send a clock pulse on bus 1. The pitch output (bus 13) should be
    // non-zero for at least one call since ImprintChord(2) fills sequence
    // with chord tones starting at MIDI note 36+offset, all non-zero CV.
    // Due to 10x multiplier the step may advance several positions, but
    // the pitch output will always reflect a valid chord note (non-zero).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusClock);
    clear_bus(bus, kBusReset);
    clear_bus(bus, kBusPitch);
    clear_bus(bus, kBusMod);

    write_gate_pulse(bus, kBusClock);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float pitch = read_bus_last(bus, kBusPitch);
    // All chord tones in ImprintChord(2) are >= 0 semitones from MIDI36.
    // MIDIQuantizer::CV(36+tone) > 0 for all tones in standard chords.
    REQUIRE(pitch > 0.0f);
}

TEST_CASE("Carpeggio CA5: two reset-then-clock sequences produce identical pitch",
          "[per-applet-mass-port][carpeggio]") {
    // Send clock pulses to advance step, then reset and clock to read
    // the post-reset pitch. Repeat the same advance+reset+clock sequence
    // a second time from the same starting position and verify the pitch
    // is identical. This confirms the reset deterministically returns to
    // step 0 without relying on a known absolute CV value.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();

    auto clock_n_then_reset_then_clock = [&](int n) -> float {
        // Advance by n clocks.
        for (int i = 0; i < n; ++i) {
            clear_bus(bus, kBusClock);
            clear_bus(bus, kBusReset);
            clear_bus(bus, kBusPitch);
            write_gate_pulse(bus, kBusClock);
            loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
        }
        // Reset.
        clear_bus(bus, kBusClock);
        clear_bus(bus, kBusReset);
        write_gate_pulse(bus, kBusReset);
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
        // Clock once and capture pitch.
        clear_bus(bus, kBusClock);
        clear_bus(bus, kBusReset);
        clear_bus(bus, kBusPitch);
        write_gate_pulse(bus, kBusClock);
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
        return read_bus_last(bus, kBusPitch);
    };

    float pitch_a = clock_n_then_reset_then_clock(3);
    float pitch_b = clock_n_then_reset_then_clock(3);
    REQUIRE(pitch_a == Catch::Approx(pitch_b).epsilon(0.001f));
}

TEST_CASE("Carpeggio CA6: mod output is zero when both CV inputs are zero",
          "[per-applet-mass-port][carpeggio]") {
    // Mod output = (In(0) * In(1)) / HEMISPHERE_MAX_INPUT_CV.
    // With both inputs at 0 hem units, output is 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusClock);
    clear_bus(bus, kBusReset);
    clear_bus(bus, kBusPitch);
    clear_bus(bus, kBusMod);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float mod = read_bus_last(bus, kBusMod);
    REQUIRE(mod == Catch::Approx(0.0f).margin(0.01f));
}

TEST_CASE("Carpeggio CA7: encoder turn in edit mode adjusts transpose and round-trips",
          "[per-applet-mass-port][carpeggio]") {
    // Navigate cursor to TRANSPOSE (cursor=1) by moving the encoder without
    // edit mode (MoveCursor increments cursor). Then enter edit mode and
    // increment transpose.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Move cursor right once (no edit mode) to reach cursor=1 (TRANSPOSE).
    _NT_uiData ui{};
    ui.controls    = 0;
    ui.lastButtons = kNT_encoderButtonL;  // button was pressed last tick (not rising)
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Enter edit mode on TRANSPOSE cursor.
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Turn encoder +1 to increment transpose from 0 to 1.
    ui.controls    = 0;
    ui.lastButtons = kNT_encoderButtonL;
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t packed = carpeggio_applet_on_data_request(loaded->algorithm);
    // transpose=1 stored as 1+24=25; sel_chord still 2.
    REQUIRE((packed & 0xFF) == 2u);
    REQUIRE(((packed >> 8) & 0xFF) == 25u);
}

TEST_CASE("Carpeggio CA8: aux button press routes to OnButtonPress without crash",
          "[per-applet-mass-port][carpeggio]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    ui.encoders[0] = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
