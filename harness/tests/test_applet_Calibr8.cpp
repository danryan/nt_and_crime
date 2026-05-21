// Per-applet test: Calibr8.
//
// Manifest: shim/include/applet_manifests/Calibr8.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Calibr8.h
//
// No 10x ticks-per-step concern: Calibr8 Controller() reads In() and Out() in
// a purely combinatorial fashion per channel. The Clock(0) branch only latches
// transpose_active[ch] = transpose[ch]; it does not advance an accumulator.
// Repeated controller ticks with the same input produce the same output.
// Test shape: standard (bus-level assertions are safe).
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0] = input  bus for "Clock"   (gate, default 1)
//   v[1] = input  bus for "CV 1"    (cv,   default 2)
//   v[2] = input  bus for "CV 2"    (cv,   default 3)
//   v[3] = output bus for "Pitch 1" (cv,   default 13)
//   v[4] = output mode for "Pitch 1" (default 1 = replace)
//   v[5] = output bus for "Pitch 2" (cv,   default 14)
//   v[6] = output mode for "Pitch 2" (default 1 = replace)
//
// CV conversion: 1 hem unit = 1/1536 V.
// MIDIQuantizer quantizes to semitone grid (128 hem units per semitone).
// Default applet state: scale_factor=0, offset=0, transpose=0.
// At default settings Calibr8 is a transparent pitch-quantizer: a semitone-
// aligned input passes through unchanged (NoteNumber -> CV round-trips losslessly
// on semitone boundaries). A mid-semitone input snaps to the nearest semitone.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

using Catch::Approx;

// Test seams defined in plugins/applets/Calibr8.cpp.
uint64_t calibr8_applet_on_data_request(_NT_algorithm* self);
void     calibr8_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices.
static constexpr int kBusGate   = 1;   // v[0] default (Clock gate input)
static constexpr int kBusCV1    = 2;   // v[1] default
static constexpr int kBusCV2    = 3;   // v[2] default
static constexpr int kBusPitch1 = 13;  // v[3] default
static constexpr int kBusPitch2 = 14;  // v[5] default

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant voltage across all frames of a 1-based bus.
static void write_bus_volts(float* busFrames, int bus, float volts) {
    float* dst = busFrames + (bus - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Read the last frame of a bus and return its voltage.
static float read_bus_volts(const float* busFrames, int bus) {
    return busFrames[(bus - 1) * kNumFrames + (kNumFrames - 1)];
}

// Zero buses used by this applet.
static void clear_buses(float* busFrames) {
    for (int b : {kBusGate, kBusCV1, kBusCV2, kBusPitch1, kBusPitch2}) {
        float* dst = busFrames + (b - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Helper: pack default Calibr8 state (all zeroed fields).
// scale_factor[i]=0  => stored as 500
// offset[i]=0        => stored as 100
// transpose[i]=0     => stored as 36
static uint64_t pack_default_calibr8() {
    uint64_t d = 0;
    d |= (uint64_t)500u;            // bits [0,10)  scale_factor[0]+500
    d |= (uint64_t)500u << 10;      // bits [10,10) scale_factor[1]+500
    d |= (uint64_t)100u << 20;      // bits [20,8)  offset[0]+100
    d |= (uint64_t)100u << 28;      // bits [28,8)  offset[1]+100
    d |= (uint64_t)36u  << 36;      // bits [36,7)  transpose[0]+36
    d |= (uint64_t)36u  << 43;      // bits [43,7)  transpose[1]+36
    return d;
}

TEST_CASE("Calibr8 CA1: OnDataRequest packs default state after Start", "[calibr8]") {
    // After BaseStart the applet has scale_factor=0, offset=0, transpose=0.
    // Those must round-trip through OnDataRequest as the packed default value.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = calibr8_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == pack_default_calibr8());
}

TEST_CASE("Calibr8 CA2: serialise round-trip preserves all fields", "[calibr8]") {
    // Inject non-default state and verify OnDataRequest reproduces it.
    // scale_factor[0]=100, scale_factor[1]=-50
    // offset[0]=10,        offset[1]=-20
    // transpose[0]=12,     transpose[1]=-6
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = 0;
    state_in |= (uint64_t)(100 + 500);      // scale_factor[0]+500 = 600
    state_in |= (uint64_t)(-50 + 500) << 10; // scale_factor[1]+500 = 450
    state_in |= (uint64_t)(10 + 100)  << 20; // offset[0]+100 = 110
    state_in |= (uint64_t)(-20 + 100) << 28; // offset[1]+100 = 80
    state_in |= (uint64_t)(12 + 36)   << 36; // transpose[0]+36 = 48
    state_in |= (uint64_t)(-6 + 36)   << 43; // transpose[1]+36 = 30

    calibr8_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = calibr8_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == state_in);
}

TEST_CASE("Calibr8 CA3: 0V input produces 0V output at default settings", "[calibr8]") {
    // MIDIQuantizer: NoteNumber(0)=60 (C4 with kOctaveZero=5 bias).
    // CV(60) = 0 hem units. output_cv = 0 * 1.0 + 0 = 0 = 0V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus_volts(bus, kBusCV1, 0.0f);
    write_bus_volts(bus, kBusCV2, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_bus_volts(bus, kBusPitch1);
    REQUIRE(out1 == Approx(0.0f).margin(0.001));
}

TEST_CASE("Calibr8 CA4: 1V input produces 1V output at default settings", "[calibr8]") {
    // NoteNumber(1536+32=1568): octave=1, semitone=0, midi=12+0+60=72.
    // CV(72): octave=6, semitone=0, cv=6*1536 - 5*1536 = 1536 hem = 1V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus_volts(bus, kBusCV1, 1.0f);
    write_bus_volts(bus, kBusCV2, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_bus_volts(bus, kBusPitch1);
    REQUIRE(out1 == Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("Calibr8 CA5: transpose shifts output by semitones", "[calibr8]") {
    // Set transpose[0]=12 (one octave up) via round-trip injection.
    // 0V input with 12-semitone transpose:
    // NoteNumber(0)=60, note += 12 -> 72.
    // CV(72) = 1536 hem = 1V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state = 0;
    state |= (uint64_t)500u;           // scale_factor[0]=0
    state |= (uint64_t)500u << 10;     // scale_factor[1]=0
    state |= (uint64_t)100u << 20;     // offset[0]=0
    state |= (uint64_t)100u << 28;     // offset[1]=0
    state |= (uint64_t)(12 + 36) << 36; // transpose[0]=12
    state |= (uint64_t)36u       << 43; // transpose[1]=0
    calibr8_applet_on_data_receive(loaded->algorithm, state);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus_volts(bus, kBusCV1, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_bus_volts(bus, kBusPitch1);
    REQUIRE(out1 == Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("Calibr8 CA6: channels are independent; CV2 passes through separately", "[calibr8]") {
    // With default settings both channels are independent pass-throughs.
    // CV1=0V -> Pitch1=0V; CV2=1V -> Pitch2=1V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus_volts(bus, kBusCV1, 0.0f);
    write_bus_volts(bus, kBusCV2, 1.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_bus_volts(bus, kBusPitch1);
    float out2 = read_bus_volts(bus, kBusPitch2);
    REQUIRE(out1 == Approx(0.0f).margin(0.001));
    REQUIRE(out2 == Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("Calibr8 CA7: encoder turn cycles cursor via customUi", "[calibr8]") {
    // Turning the encoder in non-edit mode advances the cursor.
    // State is unchanged; this just confirms routing does not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t before = calibr8_applet_on_data_request(loaded->algorithm);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Cursor move does not affect packed state.
    uint64_t after = calibr8_applet_on_data_request(loaded->algorithm);
    REQUIRE(before == after);
}

TEST_CASE("Calibr8 CA8: encoder button press toggles edit mode without crash", "[calibr8]") {
    // OnButtonPress toggles edit mode; no state change to packed fields.
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
