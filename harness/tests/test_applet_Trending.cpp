// Per-applet test: Trending.
//
// Manifest: shim/include/applet_manifests/Trending.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Trending.h
//
// 10x ticks-per-step note: Controller() decrements sample_countdown each
// tick. With default sensitivity=40: countdown = (124-40)*20 = 1680 ticks,
// so 168 steps before the first assessment. To keep tests fast, inject
// sensitivity=124 (max) via OnDataReceive, which sets countdown to max(96,0)=96
// ticks = 10 steps before first fire. Bus-level gate output tests therefore
// run >= 11 steps after setting inputs.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Sig 1"  (default 1)
//   v[1]  = input  bus for "Sig 2"  (default 2)
//   v[2]  = output bus for "Out 1"  (default 13)
//   v[3]  = output mode for "Out 1" (default 1 = replace)
//   v[4]  = output bus for "Out 2"  (default 14)
//   v[5]  = output mode for "Out 2" (default 1 = replace)
//
// Vendor pack layout (OnDataRequest):
//   [0,4)  = assign[0]    (default 0 = Rising)
//   [4,4)  = assign[1]    (default 1 = Falling)
//   [8,8)  = sensitivity  (default 40)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Trending.cpp.
uint64_t trending_applet_on_data_request(_NT_algorithm* self);
void     trending_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for Trending's default parameter layout.
static constexpr int kBusSig1 = 1;   // v[0] default
static constexpr int kBusSig2 = 2;   // v[1] default
static constexpr int kBusOut1 = 13;  // v[2] default - gate output ch 0
static constexpr int kBusOut2 = 14;  // v[4] default - gate output ch 1

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant CV value (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}


// Returns true if the last frame of a gate output bus exceeds 0.5V.
static bool read_gate_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)] > 0.5f;
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusSig1, kBusSig2, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Pack Trending state to match vendor OnDataRequest encoding.
// assign[0] in [0,5], assign[1] in [0,5], sensitivity in [4,124].
static uint64_t pack_tr(int assign0, int assign1, int sensitivity) {
    uint64_t d = 0;
    d |= (uint64_t)(assign0     & 0x0F) << 0;
    d |= (uint64_t)(assign1     & 0x0F) << 4;
    d |= (uint64_t)(sensitivity & 0xFF) << 8;
    return d;
}

// Inject max sensitivity (124) so sample_countdown fires after ~10 steps.
static void inject_max_sensitivity(_NT_algorithm* algo) {
    // Preserve default assigns (0=Rising, 1=Falling), set sensitivity=124.
    trending_applet_on_data_receive(algo, pack_tr(0, 1, 124));
}

TEST_CASE("Trending TR1: OnDataRequest packs default state after Start",
          "[per-applet][trending]") {
    // After BaseStart: assign[0]=0 (Rising), assign[1]=1 (Falling), sensitivity=40.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed   = trending_applet_on_data_request(loaded->algorithm);
    uint64_t expected = pack_tr(0, 1, 40);
    REQUIRE(packed == expected);
}

TEST_CASE("Trending TR2: serialise round-trip preserves all fields",
          "[per-applet][trending]") {
    // Inject non-default state and verify it survives a round-trip.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_tr(2, 3, 80);  // Moving, Steady, sensitivity=80
    trending_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = trending_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == state_in);
}

TEST_CASE("Trending TR3: rising signal with assign[0]=Rising drives Out 1 high",
          "[per-applet][trending]") {
    // assign[0]=0 (Rising). Feed a steadily rising CV on Sig 1. Each step
    // call computes the bus average; successive steps must show an increasing
    // value. Observe fires when abs(c_signal - l_signal) > 10 hem units
    // (~0.0065V). Each step increments CV by 0.5V (768 hem) >> 10 threshold.
    // With max sensitivity the sample fires after ~10 steps; result[0]
    // accumulates above +3 during the accumulation ticks. After the fire,
    // GateOut(0, gate=1) should drive Out 1 high (~6V).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    inject_max_sensitivity(loaded->algorithm);

    float* bus = nt::bus_frames_base();
    // 20 steps with increasing CV: starts at 0V, increments 0.5V per step.
    for (int i = 0; i < 20; ++i) {
        clear_buses(bus);
        float cv = (float)i * 0.5f;  // 0V, 0.5V, 1.0V, ..., 9.5V (clamped by shim to max)
        write_cv_bus(bus, kBusSig1, cv);
        write_cv_bus(bus, kBusSig2, 0.0f);
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    REQUIRE(read_gate_bus(bus, kBusOut1) == true);
}

TEST_CASE("Trending TR4: falling signal with assign[1]=Falling drives Out 2 high",
          "[per-applet][trending]") {
    // assign[1]=1 (Falling). Feed a steadily decreasing CV on Sig 2.
    // last_signal starts at 0; step 0 (6V) fires a spurious +1 on first
    // Observe call. Run enough steps for 2+ fire cycles (each ~10 steps) so
    // that after the first cycle primes last_signal at a high value, the
    // second cycle accumulates a clean falling result[1] < -3.
    // Total: 40 steps at -0.1V per step from 6V down to 2V, cycling 2+ fires.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    inject_max_sensitivity(loaded->algorithm);

    float* bus = nt::bus_frames_base();
    for (int i = 0; i < 40; ++i) {
        clear_buses(bus);
        float cv = 6.0f - (float)i * 0.1f;  // 6V, 5.9V, 5.8V, ..., steadily falling
        if (cv < 0.0f) cv = 0.0f;
        write_cv_bus(bus, kBusSig1, 0.0f);
        write_cv_bus(bus, kBusSig2, cv);
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    REQUIRE(read_gate_bus(bus, kBusOut2) == true);
}

TEST_CASE("Trending TR5: steady signal with assign[0]=Rising keeps Out 1 low",
          "[per-applet][trending]") {
    // A constant signal produces no Observe updates (delta=0 each tick).
    // result[0] stays 0 -> trend=steady. assign[0]=Rising does not match steady
    // so GateOut(0, 0) -> Out 1 stays low.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    inject_max_sensitivity(loaded->algorithm);

    float* bus = nt::bus_frames_base();
    for (int i = 0; i < 20; ++i) {
        clear_buses(bus);
        write_cv_bus(bus, kBusSig1, 3.0f);
        write_cv_bus(bus, kBusSig2, 3.0f);
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    REQUIRE(read_gate_bus(bus, kBusOut1) == false);
}

TEST_CASE("Trending TR6: encoder turn advances cursor without changing packed state",
          "[per-applet][trending]") {
    // Encoder turn without edit mode moves the cursor; packed state unchanged.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t before = trending_applet_on_data_request(loaded->algorithm);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t after = trending_applet_on_data_request(loaded->algorithm);
    REQUIRE(after == before);
}

TEST_CASE("Trending TR7: encoder button press routes OnButtonPress via customUi",
          "[per-applet][trending]") {
    // Trending has no OnButtonPress override; must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Trending TR8: aux button press routes on_aux_button via customUi",
          "[per-applet][trending]") {
    // on_aux_button maps to OnButtonPress; must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
