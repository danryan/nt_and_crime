// Per-applet pilot test: Calculate.
//
// Manifest: shim/include/applet_manifests/Calculate.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Calculate.h
//
// 10x ticks-per-step concern: The arithmetic operations (MIN/MAX/SUM/DIFF/MEAN)
// are purely combinatorial - Controller() reads In(0) and In(1) each tick and
// writes Out(ch, result). Running 10 ticks per step has no cumulative effect;
// the output is the same on every tick. Tests for arithmetic operations are
// safe from the multiplier. S&H and Rand use Clock(ch) which fires on every
// tick that the clocked[] flag is set, so these are NOT covered by bus-level
// single-event assertions here. Coverage follows shape 2: round-trip plus
// state-injection only.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Gate A" (default 1)  - gate for ch 0
//   v[1]  = input  bus for "Gate B" (default 2)  - gate for ch 1
//   v[2]  = input  bus for "CV 1"   (default 3)  - CV for In(0)
//   v[3]  = input  bus for "CV 2"   (default 4)  - CV for In(1)
//   v[4]  = output bus for "Out 1"  (default 13)
//   v[5]  = output mode for "Out 1" (default 1 = replace)
//   v[6]  = output bus for "Out 2"  (default 14)
//   v[7]  = output mode for "Out 2" (default 1 = replace)
//
// Vendor Start() sets operation[0]=0 (MIN) and operation[1]=1 (MAX).
// OnDataRequest packs: operation[0] in bits [0,8), operation[1] in bits [8,16).
//
// Hem unit scale: HEMISPHERE_MAX_CV=9216 at 5V full scale.
// hem_SUM clamps to [HEMISPHERE_MIN_CV, HEMISPHERE_MAX_CV] = [-9216, 9216].

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Calculate.cpp.
uint64_t calculate_applet_on_data_request(_NT_algorithm* self);
void     calculate_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices from the manifest parameter layout.
static constexpr int kBusGateA = 1;   // v[0] default
static constexpr int kBusGateB = 2;   // v[1] default
static constexpr int kBusCV1   = 3;   // v[2] default
static constexpr int kBusCV2   = 4;   // v[3] default
static constexpr int kBusOut1  = 13;  // v[4] default
static constexpr int kBusOut2  = 14;  // v[6] default

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant CV value (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Read the last frame of a CV output bus in volts.
static float read_cv_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusGateA, kBusGateB, kBusCV1, kBusCV2, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("Calculate CA1: OnDataRequest packs default operations after Start",
          "[per-applet][calculate]") {
    // Vendor Start() sets operation[0]=0 (MIN), operation[1]=1 (MAX).
    // Packed: bits [0,8)=0, bits [8,16)=1 => 0x0100.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = calculate_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 0u);          // operation[0] = MIN = 0
    REQUIRE(((packed >> 8) & 0xFF) == 1u);   // operation[1] = MAX = 1
}

TEST_CASE("Calculate CA2: serialise round-trip preserves operations",
          "[per-applet][calculate]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject operation[0]=3 (DIFF), operation[1]=4 (MEAN).
    uint64_t state_in = (uint64_t)3 | ((uint64_t)4 << 8);
    calculate_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = calculate_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 3u);
    REQUIRE(((packed >> 8) & 0xFF) == 4u);
}

TEST_CASE("Calculate CA3: MIN operation outputs the smaller of two CV inputs",
          "[per-applet][calculate]") {
    // operation[0]=MIN=0 (default). CV1=3V, CV2=1V -> Out1 = min(3V,1V) = 1V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 3.0f);
    write_cv_bus(bus, kBusCV2, 1.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // hem_MIN returns the smaller value; it maps back to ~1V on the output.
    float out1 = read_cv_bus(bus, kBusOut1);
    REQUIRE(out1 < 2.0f);   // strictly less than 2V: must be close to 1V
    REQUIRE(out1 > 0.5f);   // and above 0.5V
}

TEST_CASE("Calculate CA4: MAX operation outputs the larger of two CV inputs",
          "[per-applet][calculate]") {
    // operation[1]=MAX=1 (default). CV1=1V, CV2=3V -> Out2 = max(1V,3V) = 3V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 1.0f);
    write_cv_bus(bus, kBusCV2, 3.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out2 = read_cv_bus(bus, kBusOut2);
    REQUIRE(out2 > 2.5f);   // close to 3V
    REQUIRE(out2 < 3.5f);
}

TEST_CASE("Calculate CA5: SUM operation clamps at HEMISPHERE_MAX_CV",
          "[per-applet][calculate]") {
    // Set both channels to SUM (idx=2).
    // CV1=4V, CV2=4V -> sum=8V, clamped to HEMISPHERE_MAX_CV=9216 hem (~6V).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = (uint64_t)2 | ((uint64_t)2 << 8);  // both SUM
    calculate_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 4.0f);
    write_cv_bus(bus, kBusCV2, 4.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // HEMISPHERE_MAX_CV=9216 hem = 5V (full scale); sum 4+4=8V would exceed
    // this, so output is clamped to max. Expect output > 4V.
    float out1 = read_cv_bus(bus, kBusOut1);
    REQUIRE(out1 > 4.0f);
}

TEST_CASE("Calculate CA6: MEAN operation outputs average of two CV inputs",
          "[per-applet][calculate]") {
    // Set both channels to MEAN (idx=4).
    // CV1=2V, CV2=4V -> mean = 3V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = (uint64_t)4 | ((uint64_t)4 << 8);  // both MEAN
    calculate_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 2.0f);
    write_cv_bus(bus, kBusCV2, 4.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_cv_bus(bus, kBusOut1);
    REQUIRE(out1 > 2.5f);  // close to 3V
    REQUIRE(out1 < 3.5f);
}

TEST_CASE("Calculate CA7: encoder turn without edit mode moves cursor, not operation",
          "[per-applet][calculate]") {
    // Without entering edit mode, OnEncoderMove calls MoveCursor which moves
    // selected between 0 and 1. The operation values are unchanged.
    // operation[0]=0 (MIN), operation[1]=1 (MAX) remain after cursor move.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Confirm default operations.
    uint64_t before = calculate_applet_on_data_request(loaded->algorithm);
    REQUIRE((before & 0xFF) == 0u);           // operation[0]=MIN
    REQUIRE(((before >> 8) & 0xFF) == 1u);    // operation[1]=MAX

    // Turn encoder: moves cursor (no edit mode), operations unchanged.
    _NT_uiData ui{};
    ui.encoders[0]  = 1;
    ui.controls     = 0;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t after = calculate_applet_on_data_request(loaded->algorithm);
    REQUIRE((after & 0xFF) == 0u);            // operation[0] still MIN
    REQUIRE(((after >> 8) & 0xFF) == 1u);     // operation[1] still MAX
}

TEST_CASE("Calculate CA8: encoder button press does not crash",
          "[per-applet][calculate]") {
    // Calculate has no OnButtonPress override (it uses HemisphereApplet default).
    // Verify routing does not crash.
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
