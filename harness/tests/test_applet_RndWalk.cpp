// Per-applet test: RndWalk.
//
// Manifest: shim/include/applet_manifests/RndWalk.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/RndWalk.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer.
//   A single rising edge on a gate input asserts clocked[ch] = true across
//   all 10 inner Controller() calls. RndWalk advances currentVal inside
//   if (Clock(ch)), so one bus-level rising edge fires the random walk step
//   up to 10 times per buffer.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count and exact-value assertions on the walk outputs are
//   dropped due to non-deterministic RNG. Behavioral coverage confirms:
//     - outputs are bounded within max_val after clocking
//     - serialise round-trip preserves all parameters
//     - encoder routing works
//
// Bus parameter layout (per emit_base_parameters, 4 inputs + 2 outputs):
//   v[0]  = input bus for "X Clk"  (default 1)
//   v[1]  = input bus for "Y Clk"  (default 2)
//   v[2]  = input bus for "Range"  (default 3)
//   v[3]  = input bus for "Step"   (default 4)
//   v[4]  = output bus for "X"     (default 13)
//   v[5]  = output mode for "X"    (default 1 = replace)
//   v[6]  = output bus for "Y"     (default 14)
//   v[7]  = output mode for "Y"    (default 1 = replace)
//
// OnDataRequest default state (after Start()):
//   bits [0,1)  = yClkSrc   = 0
//   bits [1,4)  = yClkDiv   = 1
//   bits [5,8)  = range     = 20
//   bits [13,8) = step      = 20
//   bits [21,8) = smoothness = 20
//   bits [29,2) = cvRange   = 3

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <cstring>
#include <cmath>

// Test seams defined in plugins/applets/RndWalk.cpp.
uint64_t rndwalk_applet_on_data_request(_NT_algorithm* self);
void     rndwalk_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

namespace {

constexpr int kXClkBus  = 1;   // v[0] default
constexpr int kYClkBus  = 2;   // v[1] default
constexpr int kXOutBus  = 13;  // v[4] default
constexpr int kYOutBus  = 14;  // v[6] default
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_buses(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single-sample rising-edge pulse at frame 0 on the given 1-based bus.
void pulse_bus(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    slice[0] = 6.0f;
}

// Read the last frame value from a 1-based CV output bus.
float read_cv_last(const float* bus, int bus_1based) {
    return bus[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Build a packed data word matching vendor OnDataRequest layout.
// Fields match vendor pack/unpack calls verbatim.
uint64_t pack_rndwalk(int yClkSrc, int yClkDiv, int range, int step,
                       int smoothness, int cvRange) {
    uint64_t data = 0;
    data |= ((uint64_t)(yClkSrc  & 0x1))  << 0;
    data |= ((uint64_t)(yClkDiv  & 0xF))  << 1;
    data |= ((uint64_t)(range    & 0xFF)) << 5;
    data |= ((uint64_t)(step     & 0xFF)) << 13;
    data |= ((uint64_t)(smoothness & 0xFF)) << 21;
    data |= ((uint64_t)(cvRange  & 0x3))  << 29;
    return data;
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
    clear_buses(bus);
    return {loaded, loaded->algorithm, bus};
}

}  // namespace

TEST_CASE("RndWalk RW1: OnDataRequest packs default state after Start",
          "[per-applet][rndwalk]") {
    // Verify default field values match vendor Start() defaults:
    //   yClkSrc=0, yClkDiv=1, range=20, step=20, smoothness=20, cvRange=3.
    auto s = make_setup();

    uint64_t packed = rndwalk_applet_on_data_request(s.alg);
    REQUIRE(((packed >> 0) & 0x1)  == 0u);   // yClkSrc
    REQUIRE(((packed >> 1) & 0xF)  == 1u);   // yClkDiv
    REQUIRE(((packed >> 5) & 0xFF) == 20u);  // range
    REQUIRE(((packed >> 13) & 0xFF) == 20u); // step
    REQUIRE(((packed >> 21) & 0xFF) == 20u); // smoothness
    REQUIRE(((packed >> 29) & 0x3) == 3u);   // cvRange
}

TEST_CASE("RndWalk RW2: serialise round-trip preserves all parameters",
          "[per-applet][rndwalk]") {
    // Inject custom state and confirm it survives an OnDataRequest/OnDataReceive
    // round-trip. This verifies the pack/unpack implementation is symmetric.
    auto s = make_setup();

    uint64_t state_in = pack_rndwalk(
        /*yClkSrc=*/1, /*yClkDiv=*/8, /*range=*/100,
        /*step=*/50,   /*smoothness=*/200, /*cvRange=*/2);

    rndwalk_applet_on_data_receive(s.alg, state_in);
    uint64_t packed = rndwalk_applet_on_data_request(s.alg);

    REQUIRE(((packed >> 0) & 0x1)   == 1u);    // yClkSrc
    REQUIRE(((packed >> 1) & 0xF)   == 8u);    // yClkDiv
    REQUIRE(((packed >> 5) & 0xFF)  == 100u);  // range
    REQUIRE(((packed >> 13) & 0xFF) == 50u);   // step
    REQUIRE(((packed >> 21) & 0xFF) == 200u);  // smoothness
    REQUIRE(((packed >> 29) & 0x3)  == 2u);    // cvRange
}

TEST_CASE("RndWalk RW3: X output stays zero without a clock edge",
          "[per-applet][rndwalk]") {
    // With no gate input asserted, Controller never enters the walk branch.
    // currentVal stays 0, currentOut converges toward 0. X output = 0V.
    auto s = make_setup();
    clear_buses(s.bus);

    // Run several steps with no clock input.
    for (int i = 0; i < 5; ++i) {
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    float x_out = read_cv_last(s.bus, kXOutBus);
    REQUIRE(std::fabs(x_out) < 0.01f);
}

TEST_CASE("RndWalk RW4: X output is non-zero after clocking with non-zero range and step",
          "[per-applet][rndwalk]") {
    // Set range=200 and step=200 (large, deterministic bounds ensure a walk
    // step occurs). Pulse X clock 3 times; with the 10x multiplier each pulse
    // fires the walk step 10 times. The output must be non-zero.
    // Due to RNG non-determinism we only assert boundedness and non-zero-ness.
    auto s = make_setup();

    // Inject large range and step so the walk always moves.
    uint64_t state_in = pack_rndwalk(0, 1, 200, 200, 0, 3);
    rndwalk_applet_on_data_receive(s.alg, state_in);

    // Drive several clock pulses, clearing between them to avoid re-triggering.
    for (int pulse = 0; pulse < 3; ++pulse) {
        clear_buses(s.bus);
        pulse_bus(s.bus, kXClkBus);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
        clear_buses(s.bus);
        // Run an extra no-clock step so smoothing can settle.
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    float x_out = read_cv_last(s.bus, kXOutBus);
    // Output is bounded to [-HEMISPHERE_MAX_CV, HEMISPHERE_MAX_CV] = ~+/-6V.
    REQUIRE(std::fabs(x_out) <= 6.1f);
    // With range=200 and 30 walk steps, output should be non-zero.
    REQUIRE(std::fabs(x_out) > 0.0f);
}

TEST_CASE("RndWalk RW5: Y output responds to Y clock input independently",
          "[per-applet][rndwalk]") {
    // Pulse only the Y clock bus (bus 2 = second gate input = Clock(1)).
    // yClkSrc=1 routes Y to Clock(1), so Y walks and X stays at zero.
    auto s = make_setup();

    uint64_t state_in = pack_rndwalk(/*yClkSrc=*/1, 1, 200, 200, 0, 3);
    rndwalk_applet_on_data_receive(s.alg, state_in);

    for (int pulse = 0; pulse < 3; ++pulse) {
        clear_buses(s.bus);
        pulse_bus(s.bus, kYClkBus);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
        clear_buses(s.bus);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    float x_out = read_cv_last(s.bus, kXOutBus);
    float y_out = read_cv_last(s.bus, kYOutBus);

    // X should remain near zero (no X clock was driven).
    REQUIRE(std::fabs(x_out) < 0.01f);
    // Y should have moved.
    REQUIRE(std::fabs(y_out) > 0.0f);
}

TEST_CASE("RndWalk RW6: encoder turn increments range via customUi",
          "[per-applet][rndwalk]") {
    // Default cursor=0 (Range). One encoder click increments range from 20 to 21.
    auto s = make_setup();

    // Enter edit mode first (encoder button press toggles edit mode).
    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, ui);

    // Now turn encoder +1 to increment range.
    ui = {};
    ui.encoders[0] = 1;
    s.loaded->factory->customUi(s.alg, ui);

    uint64_t packed = rndwalk_applet_on_data_request(s.alg);
    REQUIRE(((packed >> 5) & 0xFF) == 21u);  // range incremented from 20
}

TEST_CASE("RndWalk RW7: encoder button press toggles edit mode without crash",
          "[per-applet][rndwalk]") {
    // Confirms the on_button_press routing does not crash.
    auto s = make_setup();

    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, ui);
    REQUIRE(true);
}

TEST_CASE("RndWalk RW8: aux button press routes without crash",
          "[per-applet][rndwalk]") {
    // on_aux_button maps to OnButtonPress (no-op beyond edit-mode toggle).
    auto s = make_setup();

    _NT_uiData ui{};
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, ui);
    REQUIRE(true);
}
