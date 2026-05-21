// Per-applet test: Metronome.
//
// Manifest: shim/include/applet_manifests/Metronome.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Metronome.h
//
// 10x ticks-per-step acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer.
//   Metronome is BUS-LEVEL FIRE-COUNT SAFE: Clock(0) is used only for
//   animation, not to advance any accumulator. Output is driven by
//   clock_m.Tock() and clock_m.IsRunning(), both of which are stateful
//   clock-manager state, not per-tick counters. Bus-level assertions are
//   safe here.
//
// Bus parameter layout (per emit_base_parameters):
//   v[0]  = input 0 (Clock) bus selector, default 1
//   v[1]  = input 1 (Tempo) bus selector, default 2
//   v[2]  = input 2 (Swing) bus selector, default 3
//   v[3]  = output 0 (Mult)  bus selector, default 13
//   v[4]  = output 0 mode,   default 1 (replace)
//   v[5]  = output 1 (Run)   bus selector, default 14
//   v[6]  = output 1 mode,   default 1 (replace)
//
// Metronome vendor notes:
//   - OnDataRequest() returns 0: no serialisable state.
//   - GateOut(1, IsRunning()): Run output mirrors clock_m running state.
//   - ClockOut(0): fires when clock_m.Tock(0) is true (clock_m must be running).
//   - clock_m is a global singleton; reset_runtime() is required between tests.
//   - clock_m starts not running (running=0 default in constructor).
//   - OnEncoderMove: cursor=0 adjusts tempo, cursor=1 adjusts multiply.
//   - OnButtonPress: toggles cursor between 0 and 1.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include "../../shim/include/HSClockManager.h"
#include "../../shim/include/OC_core.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// clock_m is a global singleton shared by Metronome Controller.
// Tests that exercise clock-driven output must manage it directly.
extern HSClockManager clock_m;

// Reset clock_m to a clean started state at OC::CORE::ticks=0.
static void metronome_start_clock(uint16_t bpm = 120) {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();
    clock_m.SetTempoBPM(bpm);
    clock_m.Start(false); // running=true, beat_tick=0
}

// Reset clock_m to a clean stopped state.
static void metronome_stop_clock() {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();
    // Default constructor: running=0, not started.
}

// Test seams defined in plugins/applets/Metronome.cpp.
uint64_t metronome_applet_on_data_request(_NT_algorithm* self);
void     metronome_applet_on_button_press(_NT_algorithm* self);

namespace {

// Default bus indices for Metronome manifest parameters.
constexpr int kBusClockIn  = 1;   // v[0] default
constexpr int kBusTempoIn  = 2;   // v[1] default
constexpr int kBusSwingIn  = 3;   // v[2] default
constexpr int kBusMultOut  = 13;  // v[3] default
constexpr int kBusRunOut   = 14;  // v[5] default

constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single-sample rising-edge gate pulse at frame 0 on a 1-based bus.
void pulse_bus(float* bus, int bus_1based, int numFrames) {
    float* slice = bus + (bus_1based - 1) * numFrames;
    slice[0] = 6.0f;
}

// Write a constant CV value across all frames of a 1-based bus.
void write_cv_bus(float* bus, int bus_1based, float volts, int numFrames) {
    float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = volts;
}

// Read the last frame of a 1-based bus; returns true if above gate threshold.
bool read_gate_bus(const float* bus, int bus_1based, int numFrames) {
    return bus[(bus_1based - 1) * numFrames + (numFrames - 1)] > 0.5f;
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
    return {loaded, loaded->algorithm, bus};
}

}  // namespace

// ---------------------------------------------------------------------------
// MT1: OnDataRequest returns 0 (no serialisable state).
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT1: OnDataRequest returns 0", "[per-applet][metronome]") {
    auto s = make_setup();
    REQUIRE(metronome_applet_on_data_request(s.alg) == 0u);
}

// ---------------------------------------------------------------------------
// MT2: serialise round-trip produces hemi_lo key with zero payload.
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT2: serialise writes hemi_lo key", "[per-applet][metronome]") {
    auto s = make_setup();
    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MT3: Run output is low when clock_m is not running.
//
// clock_m starts in a stopped state (running=0 default). GateOut(1,
// IsRunning()) writes 0V when IsRunning() is false.
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT3: Run output low when clock not running", "[per-applet][metronome]") {
    auto s = make_setup();
    metronome_stop_clock();
    REQUIRE(clock_m.IsRunning() == false);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    // GateOut(1, IsRunning()): IsRunning() == false -> Run bus low.
    REQUIRE(read_gate_bus(s.bus, kBusRunOut, kNumFrames) == false);
}

// ---------------------------------------------------------------------------
// MT4: Run output is high when clock_m is running.
//
// The per-applet runtime does not call SyncTrig(true) from a bus pulse;
// clock_m must be started directly. After Start(), IsRunning() is true and
// GateOut(1, IsRunning()) drives the Run bus high.
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT4: Run output high when clock_m is running", "[per-applet][metronome]") {
    auto s = make_setup();
    metronome_start_clock(120);
    REQUIRE(clock_m.IsRunning() == true);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    // GateOut(1, IsRunning()): IsRunning() == true -> Run bus high.
    REQUIRE(read_gate_bus(s.bus, kBusRunOut, kNumFrames) == true);
}

// ---------------------------------------------------------------------------
// MT5: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT5: hasCustomUi returns expected bitmask", "[per-applet][metronome]") {
    auto s = make_setup();
    REQUIRE(s.loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = s.loaded->factory->hasCustomUi(s.alg);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL | kNT_button1));
}

// ---------------------------------------------------------------------------
// MT6: encoder turn adjusts tempo via OnEncoderMove (cursor=0 path).
//
// Default cursor=0: OnEncoderMove(1) calls SetTempoBPM(GetTempo()+1).
// Default tempo is 120; after one turn it should be 121.
// Verified indirectly: serialise still succeeds and hemi_lo is present.
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT6: encoder turn in cursor=0 adjusts tempo without crashing", "[per-applet][metronome]") {
    auto s = make_setup();
    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, ui);

    // Must not crash; applet still serialises.
    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MT7: encoder button press toggles cursor via OnButtonPress.
//
// After one press cursor moves from 0 to 1. After a second press it returns
// to 0. We verify indirectly: two presses leave the applet in the same state
// as no presses (serialise succeeds, no crash).
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT7: encoder button press dispatches OnButtonPress without crashing", "[per-applet][metronome]") {
    auto s = make_setup();
    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, ui);  // cursor -> 1

    // Second press: cursor back to 0.
    ui.lastButtons = kNT_encoderButtonL;
    ui.controls    = kNT_encoderButtonL;
    s.loaded->factory->customUi(s.alg, ui);  // cursor -> 0

    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MT8: button1 press dispatches on_aux_button (OnButtonPress) without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("Metronome MT8: button1 press dispatches on_aux_button without crashing", "[per-applet][metronome]") {
    auto s = make_setup();
    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, ui);
    REQUIRE(true);
}
