// Per-applet pilot test: Slew.
//
// Manifest: shim/include/applet_manifests/Slew.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Slew.h
//
// The Slew applet smooths CV transitions using configurable rise/fall times.
// When the gate input for a channel is high, slewing is defeated and the output
// tracks the input directly (no smoothing). With gate low the output approaches
// the input over time controlled by rise (rising signal) and fall (falling signal).
//
// Defaults after Start(): rise=50, fall=50.
// OnDataRequest packs: bits [0,8) = rise, bits [8,16) = fall.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Defeat 1" (gate, default 1)
//   v[1]  = input  bus for "Defeat 2" (gate, default 2)
//   v[2]  = input  bus for "CV 1"     (cv,   default 3)
//   v[3]  = input  bus for "CV 2"     (cv,   default 4)
//   v[4]  = output bus for "Out 1"    (cv,   default 13)
//   v[5]  = output mode for "Out 1"   (default 1 = replace)
//   v[6]  = output bus for "Out 2"    (cv,   default 14)
//   v[7]  = output mode for "Out 2"   (default 1 = replace)
//
// 10x multiplier note: Slew's Controller() uses the simfloat accumulator
// signal[ch] which converges each tick. Running 10 ticks per step means
// more slewing per step than 1 tick. Tests do not assert exact intermediate
// values; they assert that gate-defeat produces immediate tracking and that
// without gate the output moves toward (not away from) the target.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Slew.cpp.
uint64_t slew_applet_on_data_request(_NT_algorithm* self);
void     slew_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices for Slew's parameter layout.
static constexpr int kBusDefeat1 = 1;   // v[0]
static constexpr int kBusDefeat2 = 2;   // v[1]
static constexpr int kBusCV1     = 3;   // v[2]
static constexpr int kBusCV2     = 4;   // v[3]
static constexpr int kBusOut1    = 13;  // v[4]
static constexpr int kBusOut2    = 14;  // v[6]

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Hemispheres CV scale: 6144 hem units per volt, 1.0f bus = 1V.
static constexpr float kNT_volts_per_hem = 1.0f / 6144.0f;

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static void write_gate_bus(float* busFrames, int bus_1based, bool high) {
    float v = high ? 5.0f : 0.0f;
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = v;
}

static float read_last_frame(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_all_buses(float* busFrames) {
    // Clear buses used by this plug-in.
    for (int bus : {kBusDefeat1, kBusDefeat2, kBusCV1, kBusCV2, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("Slew SL1: OnDataRequest packs rise=50 and fall=50 after Start", "[per-applet-pilot][slew]") {
    // Vendor Start() sets rise=50, fall=50.
    // OnDataRequest packs rise in bits [0,8) and fall in bits [8,16).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = slew_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF)        == 50u);  // rise
    REQUIRE(((packed >> 8) & 0xFF) == 50u);  // fall
}

TEST_CASE("Slew SL2: serialise round-trip preserves rise and fall", "[per-applet-pilot][slew]") {
    // Inject rise=100, fall=150 and confirm both survive a round-trip.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = 100u | (150u << 8);
    slew_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = slew_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF)        == 100u);
    REQUIRE(((packed >> 8) & 0xFF) == 150u);
}

TEST_CASE("Slew SL3: gate high defeats slew; output tracks input directly", "[per-applet-pilot][slew]") {
    // With gate (Defeat 1) high, Controller() immediately sets signal[0] = input.
    // After one step, Out(0) should equal the CV input (no smoothing lag).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);
    write_gate_bus(bus, kBusDefeat1, true);  // gate high: defeat slew
    write_cv_bus(bus, kBusCV1, 4.0f);        // 4V target

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out = read_last_frame(bus, kBusOut1);
    // The output should closely match 4V (within 0.1V tolerance for bus conversion).
    REQUIRE(out == Catch::Approx(4.0f).margin(0.1f));
}

TEST_CASE("Slew SL4: gate low; output moves toward target but lags (slewing active)", "[per-applet-pilot][slew]") {
    // With gate low and a large CV step, one step of slewing moves the output
    // toward (but not all the way to) the target.
    // Signal starts at 0V (after Start), target = 5V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);
    write_gate_bus(bus, kBusDefeat1, false);  // gate low: slew active
    write_cv_bus(bus, kBusCV1, 5.0f);         // large step target

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out = read_last_frame(bus, kBusOut1);
    // Output must be positive (moved toward 5V) but should not have fully reached 5V.
    REQUIRE(out > 0.0f);
    REQUIRE(out < 5.0f);
}

TEST_CASE("Slew SL5: encoder turn advances rise via customUi", "[per-applet-pilot][slew]") {
    // Cursor starts at 0 (rise). Encoder turn +1 increments rise from 50 to 51.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Confirm default.
    REQUIRE((slew_applet_on_data_request(loaded->algorithm) & 0xFF) == 50u);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE((slew_applet_on_data_request(loaded->algorithm) & 0xFF) == 51u);
}

TEST_CASE("Slew SL6: button press toggles cursor; encoder then advances fall", "[per-applet-pilot][slew]") {
    // Default cursor=0 (rise). Button press -> cursor=1 (fall).
    // Encoder +1 then increments fall from 50 to 51.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Press button to move cursor to fall.
    _NT_uiData ui_btn{};
    ui_btn.encoders[0] = 0;
    ui_btn.controls    = kNT_encoderButtonL;
    ui_btn.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_btn);

    // Turn encoder +1; should affect fall now.
    _NT_uiData ui_enc{};
    ui_enc.encoders[0] = 1;
    ui_enc.controls    = 0;
    ui_enc.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_enc);

    uint64_t packed = slew_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF)        == 50u);  // rise unchanged
    REQUIRE(((packed >> 8) & 0xFF) == 51u);  // fall incremented
}

TEST_CASE("Slew SL7: channel 2 gate high defeats slew on Out 2", "[per-applet-pilot][slew]") {
    // Gate 2 (Defeat 2) high causes Out(1) = In(1) directly.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);
    write_gate_bus(bus, kBusDefeat1, false); // ch0: slew active
    write_gate_bus(bus, kBusDefeat2, true);  // ch1: gate defeats slew
    write_cv_bus(bus, kBusCV1, 0.0f);
    write_cv_bus(bus, kBusCV2, 3.0f);        // 3V target for ch1

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out2 = read_last_frame(bus, kBusOut2);
    REQUIRE(out2 == Catch::Approx(3.0f).margin(0.1f));
}

TEST_CASE("Slew SL8: aux button routes on_aux_button via customUi without crash", "[per-applet-pilot][slew]") {
    // Slew maps on_aux_button to OnButtonPress (cursor toggle). Must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
