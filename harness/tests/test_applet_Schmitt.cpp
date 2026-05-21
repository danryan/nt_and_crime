// Per-applet test: Schmitt.
//
// Manifest: shim/include/applet_manifests/Schmitt.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Schmitt.h
//
// No 10x ticks-per-step concern: Schmitt's Controller() is purely
// combinatorial hysteresis logic. It reads In(ch) and updates state[ch]
// based on the high/low thresholds, then calls GateOut. No accumulator
// or clock-driven state advance occurs; running 10 inner ticks with
// constant CV input produces the same steady-state gate output as 1 tick.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "CV 1"    (default 1)
//   v[1]  = input  bus for "CV 2"    (default 2)
//   v[2]  = output bus for "Gate 1"  (default 13)
//   v[3]  = output mode for "Gate 1" (default 1 = replace)
//   v[4]  = output bus for "Gate 2"  (default 14)
//   v[5]  = output mode for "Gate 2" (default 1 = replace)
//
// Vendor threshold defaults (from Start()):
//   low  = 3200 hem  (~2.08 V)
//   high = 3968 hem  (~2.58 V)
//
// Hysteresis behavior:
//   state starts false; CV > high -> state=1 (gate high ~6V)
//   state=1; CV < low  -> state=0 (gate low 0V)
//   Between low and high: state unchanged.
//
// Serialisation (OnDataRequest):
//   bits [0,16)  = low   (uint16_t)
//   bits [16,16) = high  (uint16_t)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Schmitt.cpp.
uint64_t schmitt_applet_on_data_request(_NT_algorithm* self);
void     schmitt_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for Schmitt's default parameter layout.
static constexpr int kBusCV1   = 1;   // v[0] default
static constexpr int kBusCV2   = 2;   // v[1] default
static constexpr int kBusGate1 = 13;  // v[2] default - gate output 1
static constexpr int kBusGate2 = 14;  // v[4] default - gate output 2

// Frames per step call matching the host sim default (numFramesBy4=8).
static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// ONE_OCTAVE = 1536 hem units per volt.
static constexpr float kHemPerVolt = 1536.0f;

// Vendor defaults (from Schmitt::Start()).
static constexpr uint16_t kDefaultLow  = 3200;
static constexpr uint16_t kDefaultHigh = 3968;

// Write a constant CV value (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Returns true if the last frame of the given bus exceeds 0.5V (gate high).
static bool read_gate_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)] > 0.5f;
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusCV1, kBusCV2, kBusGate1, kBusGate2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("Schmitt SC1: OnDataRequest packs low=3200 high=3968 after Start",
          "[per-applet][schmitt]") {
    // Vendor Start() sets low=3200, high=3968.
    // bits [0,16)  = 3200, bits [16,16) = 3968.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = schmitt_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFFF) == kDefaultLow);
    REQUIRE(((packed >> 16) & 0xFFFF) == kDefaultHigh);
}

TEST_CASE("Schmitt SC2: serialise round-trip preserves low and high",
          "[per-applet][schmitt]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject low=2000, high=5000.
    uint64_t state_in = ((uint64_t)5000 << 16) | (uint64_t)2000;
    schmitt_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = schmitt_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFFF) == 2000u);
    REQUIRE(((packed >> 16) & 0xFFFF) == 5000u);
}

TEST_CASE("Schmitt SC3: CV1 above high threshold drives Gate1 high",
          "[per-applet][schmitt]") {
    // Default high = 3968 hem = 3968/1536 V ~= 2.583V.
    // Apply CV1 = 3.0V (4608 hem) > high: state[0] -> 1 -> Gate1 ~6V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 3.0f);
    write_cv_bus(bus, kBusCV2, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusGate1) == true);
}

TEST_CASE("Schmitt SC4: CV1 below low threshold keeps Gate1 low from initial state",
          "[per-applet][schmitt]") {
    // state[0] starts false. CV1 < low (2.08V): state stays 0 -> Gate1 = 0V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 1.0f);  // 1V < low
    write_cv_bus(bus, kBusCV2, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusGate1) == false);
}

TEST_CASE("Schmitt SC5: hysteresis - CV1 in dead zone holds previous state",
          "[per-applet][schmitt]") {
    // Raise state[0] by going above high (3.0V), then drop into dead zone
    // (between low 2.08V and high 2.58V, e.g. 2.3V). Gate should remain high.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();

    // Step 1: raise gate by going above high.
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 3.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    REQUIRE(read_gate_bus(bus, kBusGate1) == true);

    // Step 2: drop into dead zone (2.3V = 2.3*1536 = 3533 hem; low=3200, high=3968).
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 2.3f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    REQUIRE(read_gate_bus(bus, kBusGate1) == true);
}

TEST_CASE("Schmitt SC6: CV1 falling below low threshold clears gate after being high",
          "[per-applet][schmitt]") {
    // Raise gate with 3.0V, then fall below low (1.0V) -> gate goes low.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();

    // Raise gate.
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 3.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    REQUIRE(read_gate_bus(bus, kBusGate1) == true);

    // Drop below low threshold.
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 1.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    REQUIRE(read_gate_bus(bus, kBusGate1) == false);
}

TEST_CASE("Schmitt SC7: CV2 independently controls Gate2",
          "[per-applet][schmitt]") {
    // CV1=0V (gate1 stays low), CV2=3.0V (gate2 goes high).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 0.0f);
    write_cv_bus(bus, kBusCV2, 3.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusGate1) == false);
    REQUIRE(read_gate_bus(bus, kBusGate2) == true);
}

TEST_CASE("Schmitt SC8: encoder turn adjusts high threshold via customUi",
          "[per-applet][schmitt]") {
    // cursor starts at 0 (locked). OnButtonPress increments cursor to 1.
    // Encoder move with cursor=1 adjusts low threshold.
    // OnButtonPress again -> cursor=2. Encoder adjusts high threshold.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Confirm defaults.
    uint64_t packed = schmitt_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFFF) == kDefaultLow);
    REQUIRE(((packed >> 16) & 0xFFFF) == kDefaultHigh);

    // Press button to move cursor to 1 (low threshold selected).
    _NT_uiData ui_btn{};
    ui_btn.controls    = kNT_encoderButtonL;
    ui_btn.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_btn);

    // Turn encoder +1: low adjusts by +64 (vendor: low += 64 * direction).
    _NT_uiData ui_enc{};
    ui_enc.encoders[0]  = 1;
    ui_enc.controls     = 0;
    ui_enc.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui_enc);

    packed = schmitt_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFFF) == kDefaultLow + 64);
    REQUIRE(((packed >> 16) & 0xFFFF) == kDefaultHigh);
}
