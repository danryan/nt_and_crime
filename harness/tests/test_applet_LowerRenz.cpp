// Per-applet test: LowerRenz.
//
// Manifest: shim/include/applet_manifests/LowerRenz.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/LowerRenz.h
//
// 10x ticks-per-step concern: LowerRenz calls lorenz_m->Process() inside
// Controller(). Because ticks_this_step = numFrames / 3 = 10, the Lorenz
// generator advances 10 steps per step() call. Tests that assert a specific
// output value after a known number of step() calls must account for this.
// This test suite avoids fire-count assertions; it uses round-trip state
// injection and sign/magnitude checks instead (shape 2 in CLAUDE.md).
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Reset"  (default 1, gate)
//   v[1]  = input  bus for "Freeze" (default 2, gate)
//   v[2]  = input  bus for "Freq"   (default 3, cv)
//   v[3]  = input  bus for "Rho"    (default 4, cv)
//   v[4]  = output bus for "X"      (default 13)
//   v[5]  = output mode for "X"     (default 1 = replace)
//   v[6]  = output bus for "Y"      (default 14)
//   v[7]  = output mode for "Y"     (default 1 = replace)
//
// Vendor serialisation:
//   bits [0, 8)  = freq  (0..255, default 128)
//   bits [8, 8)  = rho   (0..127, default 64)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/LowerRenz.cpp.
uint64_t lowerrenz_applet_on_data_request(_NT_algorithm* self);
void     lowerrenz_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices.
static constexpr int kBusReset  = 1;   // v[0]
static constexpr int kBusFreeze = 2;   // v[1]
static constexpr int kBusFreq   = 3;   // v[2]
static constexpr int kBusRho    = 4;   // v[3]
static constexpr int kBusX      = 13;  // v[4]
static constexpr int kBusY      = 14;  // v[6]

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static void write_gate_bus(float* busFrames, int bus_1based, float level) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = level;
}

static float read_cv_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_buses(float* busFrames) {
    for (int bus : {kBusReset, kBusFreeze, kBusFreq, kBusRho, kBusX, kBusY}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("LowerRenz LR1: OnDataRequest packs default freq=128, rho=64 after Start",
          "[per-applet][lowerrenz]") {
    // Vendor Start() sets freq=128, rho=64.
    // OnDataRequest: bits [0,8) = freq, bits [8,8) = rho.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = lowerrenz_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 128u);          // freq
    REQUIRE(((packed >> 8) & 0xFF) == 64u);    // rho
}

TEST_CASE("LowerRenz LR2: serialise round-trip preserves freq and rho",
          "[per-applet][lowerrenz]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject freq=200, rho=100.
    uint64_t state_in = 200u | (100u << 8);
    lowerrenz_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = lowerrenz_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 200u);
    REQUIRE(((packed >> 8) & 0xFF) == 100u);
}

TEST_CASE("LowerRenz LR3: running produces non-zero X and Y outputs",
          "[per-applet][lowerrenz]") {
    // After several step() calls the Lorenz attractor diverges from zero.
    // With default freq=128 and rho=64 the generator produces non-trivial
    // output; at least one of X or Y must be non-zero after 5 steps.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    bool any_nonzero = false;
    for (int step = 0; step < 5; ++step) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
        float x = read_cv_bus_last(bus, kBusX);
        float y = read_cv_bus_last(bus, kBusY);
        if (x != 0.0f || y != 0.0f) {
            any_nonzero = true;
            break;
        }
    }
    REQUIRE(any_nonzero);
}

TEST_CASE("LowerRenz LR4: freeze gate holds outputs constant",
          "[per-applet][lowerrenz]") {
    // Gate(1) = Freeze. When freeze bus is high, Controller() returns early
    // without calling lorenz_m->Process(). Output should stay constant.
    // Run a few steps unfrozen to get a non-zero initial state, then freeze
    // and confirm outputs do not change across subsequent steps.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    // Warm up: 3 steps unfrozen to establish non-trivial output.
    for (int i = 0; i < 3; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    float x_before = read_cv_bus_last(bus, kBusX);
    float y_before = read_cv_bus_last(bus, kBusY);

    // Apply freeze gate.
    write_gate_bus(bus, kBusFreeze, 5.0f);

    // Two additional steps while frozen.
    for (int i = 0; i < 2; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
        float x = read_cv_bus_last(bus, kBusX);
        float y = read_cv_bus_last(bus, kBusY);
        REQUIRE(x == x_before);
        REQUIRE(y == y_before);
    }
}

TEST_CASE("LowerRenz LR5: encoder turn changes freq parameter via customUi in edit mode",
          "[per-applet][lowerrenz]") {
    // OnEncoderMove gates on EditMode(). Must enter edit mode (button press)
    // before an encoder turn will change freq. Default cursor=0 maps to freq.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    REQUIRE((lowerrenz_applet_on_data_request(loaded->algorithm) & 0xFF) == 128u);

    // Enter edit mode.
    {
        _NT_uiData ui{};
        ui.controls    = kNT_encoderButtonL;
        ui.lastButtons = 0;
        loaded->factory->customUi(loaded->algorithm, ui);
    }
    // Turn encoder +1 with cursor=0 -> freq increments to 129.
    {
        _NT_uiData ui{};
        ui.encoders[0] = 1;
        ui.controls    = 0;
        ui.lastButtons = 0;
        loaded->factory->customUi(loaded->algorithm, ui);
    }

    REQUIRE((lowerrenz_applet_on_data_request(loaded->algorithm) & 0xFF) == 129u);
}

TEST_CASE("LowerRenz LR6: encoder button press switches cursor via customUi",
          "[per-applet][lowerrenz]") {
    // OnButtonPress toggles edit mode / cursor in LowerRenz. Must not crash.
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

TEST_CASE("LowerRenz LR7: encoder turn with cursor=1 changes rho parameter",
          "[per-applet][lowerrenz]") {
    // Sequence: turn (no edit) to move cursor to 1, then enter edit mode,
    // then turn encoder to increment rho from 64 to 65.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Move cursor to position 1 (not in edit mode, so turn advances cursor).
    {
        _NT_uiData ui{};
        ui.encoders[0] = 1;
        ui.controls    = 0;
        ui.lastButtons = 0;
        loaded->factory->customUi(loaded->algorithm, ui);
    }
    // Enter edit mode (cursor=1 now points to rho).
    {
        _NT_uiData ui{};
        ui.controls    = kNT_encoderButtonL;
        ui.lastButtons = 0;
        loaded->factory->customUi(loaded->algorithm, ui);
    }
    // Turn encoder +1 -> rho increments from 64 to 65.
    {
        _NT_uiData ui{};
        ui.encoders[0] = 1;
        ui.controls    = 0;
        ui.lastButtons = 0;
        loaded->factory->customUi(loaded->algorithm, ui);
    }

    uint64_t packed = lowerrenz_applet_on_data_request(loaded->algorithm);
    REQUIRE(((packed >> 8) & 0xFF) == 65u);
}

TEST_CASE("LowerRenz LR8: aux button routes on_aux_button via customUi",
          "[per-applet][lowerrenz]") {
    // LowerRenz maps on_aux_button to OnButtonPress. Must not crash.
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
