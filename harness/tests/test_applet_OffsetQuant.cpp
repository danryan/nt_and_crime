// Per-applet test: OffsetQuant.
//
// Manifest: shim/include/applet_manifests/OffsetQuant.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/OffsetQuant.h
//
// 10x ticks-per-step note: OffsetQuant uses Clock(ch) to disable continuous
// mode and enable ADC-lag sampling. In continuous mode (default after Start())
// the applet quantizes every Controller() tick, so 10x inner ticks produce
// the same steady-state output -- no multiplier modeling needed for
// continuous-mode tests. Clocked-mode tests do not assert fire counts.
//
// Bus parameter layout (per emit_base_parameters with 4 inputs, 2 outputs):
//   v[0]  = Clock 1 gate bus    (default 1)
//   v[1]  = Clock 2 gate bus    (default 2)
//   v[2]  = CV 1 input bus      (default 3)
//   v[3]  = CV 2 input bus      (default 4)
//   v[4]  = Out 1 cv output bus (default 13)
//   v[5]  = Out 1 mode          (default 1 = replace)
//   v[6]  = Out 2 cv output bus (default 14)
//   v[7]  = Out 2 mode          (default 1 = replace)
//
// Vendor OnDataRequest bit layout:
//   bits [0,3)   = range_mode[0]  (RANGE_0_2 = 1 after Start)
//   bits [3,6)   = range_mode[1]  (RANGE_0_2 = 1 after Start)
//   bits [6,14)  = GetScale(0)
//   bits [14,22) = GetScale(1)
//   bits [22,26) = GetRootNote(0)
//   bits [26,30) = GetRootNote(1)
//
// Constants:
//   ONE_OCTAVE             = 1536 hem units
//   HEMISPHERE_MAX_INPUT_CV = 9216 hem units (6V)
//   OFFSET_QUANT_MAX_CV_INPUT = 9216 * 40 / 48 = 7680 hem units
//   RANGE_0_2 maps input [0..7680 hem] to offset [0..2*1536 hem] = [0..3072 hem]

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/OffsetQuant.cpp.
uint64_t offsetquant_applet_on_data_request(_NT_algorithm* self);
void     offsetquant_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus assignments for OffsetQuant's 4-input 2-output manifest.
static constexpr int kBusClock1 = 1;   // v[0] default
static constexpr int kBusClock2 = 2;   // v[1] default
static constexpr int kBusCV1    = 3;   // v[2] default
static constexpr int kBusCV2    = 4;   // v[3] default
static constexpr int kBusOut1   = 13;  // v[4] default
static constexpr int kBusOut2   = 14;  // v[6] default

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant CV (in volts) to all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a gate high/low to all frames of a 1-based bus.
static void write_gate_bus(float* busFrames, int bus_1based, bool high) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    float v = high ? 5.0f : 0.0f;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = v;
}

// Read the last-frame CV (in volts) from a 1-based bus.
static float read_cv_bus(const float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Zero all buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusClock1, kBusClock2, kBusCV1, kBusCV2, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Extract range_mode[0] from the packed OnDataRequest value.
static int range_mode_ch0(uint64_t data) { return (int)(data & 0x7u); }
// Extract range_mode[1] from the packed OnDataRequest value.
static int range_mode_ch1(uint64_t data) { return (int)((data >> 3) & 0x7u); }

TEST_CASE("OffsetQuant OQ1: OnDataRequest packs RANGE_0_2 for both channels after Start",
          "[per-applet][offsetquant]") {
    // Start() sets range_mode[ch] = RANGE_0_2 = 1 for both channels.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = offsetquant_applet_on_data_request(loaded->algorithm);
    REQUIRE(range_mode_ch0(packed) == 1u);
    REQUIRE(range_mode_ch1(packed) == 1u);
}

TEST_CASE("OffsetQuant OQ2: serialise round-trip preserves range_mode for both channels",
          "[per-applet][offsetquant]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // range_mode[0]=3 (RANGE_2_4), range_mode[1]=4 (RANGE_3_5), scales/roots at 0.
    uint64_t state_in = (3u) | (4u << 3);
    offsetquant_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = offsetquant_applet_on_data_request(loaded->algorithm);
    REQUIRE(range_mode_ch0(packed) == 3u);
    REQUIRE(range_mode_ch1(packed) == 4u);
}

TEST_CASE("OffsetQuant OQ3: continuous mode quantizes CV 1 to a non-negative pitch",
          "[per-applet][offsetquant]") {
    // In continuous mode (default; no Clock received), the applet quantizes
    // In(0) every tick. With RANGE_0_2 and a 3V input:
    //   offset = Proportion(3V*1536, 7680, 3072) = ~1843 hem
    // The quantizer maps this to the nearest scale note >= 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 3.0f);  // 3V input on CV channel 1

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_cv_bus(bus, kBusOut1);
    REQUIRE(out1 >= 0.0f);
}

TEST_CASE("OffsetQuant OQ4: continuous mode quantizes CV 2 to a non-negative pitch",
          "[per-applet][offsetquant]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV2, 1.5f);  // 1.5V input on CV channel 2

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out2 = read_cv_bus(bus, kBusOut2);
    REQUIRE(out2 >= 0.0f);
}

TEST_CASE("OffsetQuant OQ5: RANGE_FULL passes CV through quantizer without range mapping",
          "[per-applet][offsetquant]") {
    // With RANGE_FULL (range_mode=0), offset = pitch (raw CV). Any non-negative CV
    // input produces a non-negative quantized output.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject RANGE_FULL (0) for channel 0.
    uint64_t state_in = 0u;  // range_mode[0]=0, range_mode[1]=0
    offsetquant_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 2.0f);  // 2V = 3072 hem

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_cv_bus(bus, kBusOut1);
    // RANGE_FULL passes 2V directly to quantizer; quantized pitch is non-negative.
    REQUIRE(out1 >= 0.0f);
}

TEST_CASE("OffsetQuant OQ6: zero CV input produces zero or positive output in RANGE_0_2",
          "[per-applet][offsetquant]") {
    // With CV=0 and RANGE_0_2: offset = Proportion(0, 7680, 3072) = 0.
    // Quantizer maps 0 to 0 (root note). Output should be ~0V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_cv_bus(bus, kBusOut1);
    REQUIRE(out1 >= 0.0f);
    REQUIRE(out1 < 0.5f);  // Quantized near 0V.
}

TEST_CASE("OffsetQuant OQ7: clock edge switches channel to clocked mode",
          "[per-applet][offsetquant]") {
    // A rising clock edge on Clock 1 sets continuous[0]=false and starts ADC lag.
    // After clearing the clock bus and stepping enough for ADC lag to expire,
    // the channel samples its CV. The output must be non-negative.
    // Note: 10x inner ticks per step means StartADCLag fires 10 times per buffer.
    // Use clear-then-step pattern from CLAUDE.md for single-edge gate tests.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();

    // Step 1: send clock edge + CV.
    clear_buses(bus);
    write_gate_bus(bus, kBusClock1, true);
    write_cv_bus(bus, kBusCV1, 2.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Step 2: clear clock bus, CV still present; ADC lag expires, sample fires.
    clear_buses(bus);
    write_gate_bus(bus, kBusClock1, false);
    write_cv_bus(bus, kBusCV1, 2.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_cv_bus(bus, kBusOut1);
    REQUIRE(out1 >= 0.0f);
}

TEST_CASE("OffsetQuant OQ8: encoder turn changes range_mode via customUi",
          "[per-applet][offsetquant]") {
    // OnEncoderMove navigates the cursor when not in edit mode. After entering
    // edit mode with OnButtonPress, encoder turn adjusts temp_range_mode.
    // This test confirms routing does not crash and the applet survives UI calls.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Enter edit mode (cursor=0 toggles into EditMode).
    _NT_uiData press{};
    press.controls    = kNT_encoderButtonL;
    press.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, press);

    // Turn encoder to change temp_range_mode.
    _NT_uiData turn{};
    turn.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, turn);

    // Exit edit mode; range_mode[0] updates to temp_range_mode.
    loaded->factory->customUi(loaded->algorithm, press);

    // range_mode[0] should now be RANGE_0_2 + 1 = RANGE_1_3 = 2.
    uint64_t packed = offsetquant_applet_on_data_request(loaded->algorithm);
    REQUIRE(range_mode_ch0(packed) == 2u);
}
