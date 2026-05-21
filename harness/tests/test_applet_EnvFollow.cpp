// Per-applet test: EnvFollow.
//
// Manifest: shim/include/applet_manifests/EnvFollow.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/EnvFollow.h
//
// 10x ticks-per-step note: Controller() decrements a countdown
// (HEM_ENV_FOLLOWER_SAMPLES=166) on every tick. With 10 ticks per step(),
// the countdown fires roughly every 17 step() calls. Tests that probe
// steady-state output must run enough steps for the countdown to fire and
// for the signal to slew to the target. Coverage here uses state-injection
// (OnDataReceive) plus round-trip; bus-level output tests run a generous
// step count to ensure convergence without asserting exact tick counts.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "CV 1"  (default 1)
//   v[1]  = input  bus for "CV 2"  (default 2)
//   v[2]  = output bus for "Out 1" (default 13)
//   v[3]  = output mode for "Out 1" (default 1 = replace)
//   v[4]  = output bus for "Out 2" (default 14)
//   v[5]  = output mode for "Out 2" (default 1 = replace)
//
// Vendor pack layout (OnDataRequest):
//   [0,5)  = gain[0]       (default 10)
//   [5,5)  = gain[1]       (default 10)
//   [10,1) = duck[0]       (default 0 = follow)
//   [11,1) = duck[1]       (default 1 = duck)
//   [12,4) = speed - 1     (default speed=1 -> packed=0)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/EnvFollow.cpp.
uint64_t envfollow_applet_on_data_request(_NT_algorithm* self);
void     envfollow_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for EnvFollow's default parameter layout.
static constexpr int kBusCV1  = 1;   // v[0] default
static constexpr int kBusCV2  = 2;   // v[1] default
static constexpr int kBusOut1 = 13;  // v[2] default
static constexpr int kBusOut2 = 14;  // v[4] default

// Frames per step call matching the host sim default (numFramesBy4=8).
static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant CV value (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Returns the value on the last frame of a 1-based bus.
static float read_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusCV1, kBusCV2, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Pack EnvFollow state to match vendor OnDataRequest encoding.
// gain[ch] in [1,31], duck[ch] in {0,1}, speed in [1,16].
static uint64_t pack_ef(int gain0, int gain1, int duck0, int duck1, int speed) {
    uint64_t d = 0;
    d |= (uint64_t)(gain0  & 0x1F) << 0;
    d |= (uint64_t)(gain1  & 0x1F) << 5;
    d |= (uint64_t)(duck0  & 0x01) << 10;
    d |= (uint64_t)(duck1  & 0x01) << 11;
    d |= (uint64_t)((speed - 1) & 0x0F) << 12;
    return d;
}

TEST_CASE("EnvFollow EF1: OnDataRequest packs default state after Start",
          "[per-applet][envfollow]") {
    // After BaseStart: gain[0]=gain[1]=10, duck[0]=0, duck[1]=1, speed=1.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed   = envfollow_applet_on_data_request(loaded->algorithm);
    uint64_t expected = pack_ef(10, 10, 0, 1, 1);
    REQUIRE(packed == expected);
}

TEST_CASE("EnvFollow EF2: serialise round-trip preserves all fields",
          "[per-applet][envfollow]") {
    // Inject non-default state and verify it survives a round-trip.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_ef(20, 5, 1, 0, 8);
    envfollow_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = envfollow_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == state_in);
}

TEST_CASE("EnvFollow EF3: zero CV input produces zero follow output after settling",
          "[per-applet][envfollow]") {
    // With CV inputs at 0V the envelope follower tracks silence.
    // After enough steps the output should converge to 0V.
    // Run 30 steps: countdown fires every ~17 steps (166 ticks / 10 per step).
    // After the second countdown fire the signal has a target of 0 and slews to 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 0.0f);
    write_cv_bus(bus, kBusCV2, 0.0f);

    for (int i = 0; i < 30; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    float out1 = read_bus_last(bus, kBusOut1);
    REQUIRE(out1 == Catch::Approx(0.0f).margin(0.05f));
}

TEST_CASE("EnvFollow EF4: positive CV input produces positive follow output after settling",
          "[per-applet][envfollow]") {
    // With CV1=3V (4608 hem) and gain=10, speed=1 (slow slew):
    // target[0] = max[0] * gain[0] = 4608 * 10 clamped to HEMISPHERE_MAX_CV=9216.
    // duck[0]=0 so output follows (not inverted).
    // After many steps the signal slews toward the clamped target (9216 hem = 6V).
    // Use speed=16 (max) to ensure fast convergence. Inject speed=16 via state.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject speed=16 so the slew converges quickly.
    uint64_t state_in = pack_ef(10, 10, 0, 1, 16);
    envfollow_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 3.0f);  // 3V = 4608 hem; 4608*10 > 9216 -> clamped to 9216 = 6V
    write_cv_bus(bus, kBusCV2, 0.0f);

    for (int i = 0; i < 80; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    float out1 = read_bus_last(bus, kBusOut1);
    // At gain=10 and any nonzero input the follower saturates to HEMISPHERE_MAX_CV=9216 hem = 6V.
    REQUIRE(out1 == Catch::Approx(6.0f).epsilon(0.05f));
}

TEST_CASE("EnvFollow EF5: duck mode inverts output for channel 2",
          "[per-applet][envfollow]") {
    // Default: duck[1]=1. With nonzero CV2 input the duck output is
    // HEMISPHERE_MAX_CV - target[1] (clamped 0..9216).
    // CV2=3V -> target=9216 (clamped) -> duck output = 9216-9216 = 0.
    // With speed=16 for fast convergence.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_ef(10, 10, 0, 1, 16);
    envfollow_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 0.0f);
    write_cv_bus(bus, kBusCV2, 3.0f);  // saturates target[1] to 9216

    for (int i = 0; i < 80; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    // Duck: out2 = 9216 - 9216 = 0 hem = 0V.
    float out2 = read_bus_last(bus, kBusOut2);
    REQUIRE(out2 == Catch::Approx(0.0f).margin(0.15f));
}

TEST_CASE("EnvFollow EF6: encoder turn moves cursor without changing packed state",
          "[per-applet][envfollow]") {
    // Encoder turn without edit mode moves the cursor; no state change expected.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t before = envfollow_applet_on_data_request(loaded->algorithm);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t after = envfollow_applet_on_data_request(loaded->algorithm);
    REQUIRE(after == before);
}

TEST_CASE("EnvFollow EF7: encoder button press routes OnButtonPress via customUi",
          "[per-applet][envfollow]") {
    // EnvFollow has no OnButtonPress override; this must not crash.
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

TEST_CASE("EnvFollow EF8: aux button press routes on_aux_button via customUi",
          "[per-applet][envfollow]") {
    // on_aux_button maps to OnButtonPress; must not crash.
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
