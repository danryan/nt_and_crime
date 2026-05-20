// Per-applet pilot test: Compare.
//
// Manifest: shim/include/applet_manifests/Compare.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Compare.h
//
// No 10x ticks-per-step concern: Compare has no clock-driven state. The
// Controller() runs each tick but its comparator logic is purely combinatorial
// (reads In(0) and mod_cv, sets GateOut). Any tick count produces the same
// steady-state output given constant CV inputs.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "CV 1"  (default 1)
//   v[1]  = input  bus for "CV 2"  (default 2)
//   v[2]  = output bus for "GT"    (default 13)
//   v[3]  = output mode for "GT"   (default 1 = replace)
//   v[4]  = output bus for "Min"   (default 14)
//   v[5]  = output mode for "Min"  (default 1 = replace)
//
// Vendor threshold formula:
//   cv_level = Proportion(level, 255, HEMISPHERE_MAX_CV=9216)
//   mod_cv   = cv_level + DetentedIn(1)   (adds In(1) with detent dead zone)
//   if (In(0) > mod_cv) -> GateOut(0, 1), GateOut(1, 0)
//   else                -> GateOut(0, 0), GateOut(1, 1)
//
// With level=128 (default):
//   cv_level = Proportion(128, 255, 9216) = ~4625 hem units (~3.01V)
//   With CV2=0: mod_cv = 4625
//   CV1 > 3.01V -> GT output high (~6V)
//   CV1 < 3.01V -> GT output low (0V)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Compare.cpp. Using these avoids
// pulling _AppletInstance into this TU (which would require the vendor
// Compare class to be in scope and risks ODR collisions).
uint64_t compare_applet_on_data_request(_NT_algorithm* self);
void     compare_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for Compare's default parameter layout.
static constexpr int kBusCV1 = 1;   // v[0] default
static constexpr int kBusCV2 = 2;   // v[1] default
static constexpr int kBusGT  = 13;  // v[2] default - gate output

// Frames per step call matching the host sim default (numFramesBy4=8).
static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant CV value (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Returns true if the last frame of the gate output bus exceeds 0.5V.
static bool read_gate_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)] > 0.5f;
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusCV1, kBusCV2, kBusGT}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("Compare CP1: OnDataRequest packs level=128 after Start", "[per-applet-pilot][compare]") {
    // Vendor Start() sets level=128. OnDataRequest packs it into bits [0,8).
    // The spec says Compare serialises level (it returns non-zero).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = compare_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 128u);
}

TEST_CASE("Compare CP2: serialise round-trip preserves level", "[per-applet-pilot][compare]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject level=200 and confirm it round-trips.
    uint64_t state_in = 200u;  // bits [0,8) = 200
    compare_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = compare_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 200u);
}

TEST_CASE("Compare CP3: In(0) above threshold drives GT high", "[per-applet-pilot][compare]") {
    // With level=128: cv_level = Proportion(128,255,9216) ~= 4625 hem (~3.01V).
    // CV1=4V=6144 hem > 4625: GateOut(0,1) fires -> GT bus ~6V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 4.0f);
    write_cv_bus(bus, kBusCV2, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusGT) == true);
}

TEST_CASE("Compare CP4: In(0) below threshold drives GT low", "[per-applet-pilot][compare]") {
    // CV1=2V=3072 hem < threshold 4625: GateOut(0,0) fires -> GT bus 0V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 2.0f);
    write_cv_bus(bus, kBusCV2, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusGT) == false);
}

TEST_CASE("Compare CP5: CV2 raises threshold via DetentedIn", "[per-applet-pilot][compare]") {
    // With level=128: cv_level ~= 4625. CV2=2V (3072 hem) added via DetentedIn:
    // mod_cv = 4625 + 3072 = 7697 hem (~5.01V).
    // CV1=4V=6144 hem < 7697: gate goes low even though CV1 > raw cv_level.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 4.0f);  // 4V
    write_cv_bus(bus, kBusCV2, 2.0f);  // 2V pushes threshold above 4V

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusGT) == false);
}

TEST_CASE("Compare CP6: encoder turn advances level via customUi", "[per-applet-pilot][compare]") {
    // Drive _NT_uiData with encoders[0]=1. The on_encoder_turn hook calls
    // OnEncoderMove(1) which increments level from default 128 to 129.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE((compare_applet_on_data_request(loaded->algorithm) & 0xFF) == 128u);

    _NT_uiData ui{};
    ui.encoders[0]  = 1;
    ui.controls     = 0;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE((compare_applet_on_data_request(loaded->algorithm) & 0xFF) == 129u);
}

TEST_CASE("Compare CP7: encoder button press routes OnButtonPress via customUi", "[per-applet-pilot][compare]") {
    // Compare's OnButtonPress is a no-op; this test confirms the routing does
    // not crash and the no-op completes cleanly.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Compare CP8: button1 press routes on_aux_button via customUi", "[per-applet-pilot][compare]") {
    // Compare maps on_aux_button to OnButtonPress (no-op). Must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
