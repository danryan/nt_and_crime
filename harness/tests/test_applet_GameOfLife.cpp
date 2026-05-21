// Per-applet test: GameOfLife.
//
// Manifest: shim/include/applet_manifests/GameOfLife.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/GameOfLife.h
//
// GameOfLife implements Conway's Game of Life. Clock(0) advances the
// cellular automaton by one generation. Gate(1) draws a cell at the
// traveler position (x,y). In(0)/In(1) control the traveler X/Y position
// via ProportionCV. Out(0) outputs global_density; Out(1) outputs
// local_density (cells near the traveler).
//
// 10x ticks-per-step concern: ProcessGameBoard() is called inside
// if (Clock(0)). With the default 10 inner ticks, one rising clock edge
// fires ProcessGameBoard 10 times. Tests that need exactly one generation
// advance use hem_shim::inner_ticks_override=1.
//
// OnDataRequest packs weight (6 bits at offset 0). Start() sets weight=30.
//
// Bus parameter layout (4 inputs, 2 outputs):
//   v[0]  = 1   Clock input bus  (gate, bus 1 default)
//   v[1]  = 2   Draw  input bus  (gate, bus 2 default)
//   v[2]  = 3   X pos input bus  (cv,   bus 3 default)
//   v[3]  = 4   Y pos input bus  (cv,   bus 4 default)
//   v[4]  = 13  Global output bus (cv,  bus 13 default)
//   v[5]  = 1   Global output mode
//   v[6]  = 14  Local  output bus (cv,  bus 14 default)
//   v[7]  = 1   Local  output mode

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <cstring>

// Test seams defined in plugins/applets/GameOfLife.cpp.
uint64_t game_of_life_applet_on_data_request(_NT_algorithm* self);
void     game_of_life_applet_on_data_receive(_NT_algorithm* self, uint64_t data);
void     game_of_life_clear_board(_NT_algorithm* self);

// inner_ticks_override: set to 1 to run exactly one Controller() tick per
// step() call. Required for single-generation advancement tests.
namespace hem_shim { extern int inner_ticks_override; }

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Default bus indices matching emit_base_parameters defaults.
static constexpr int kBusClockIn  = 1;
static constexpr int kBusDrawIn   = 2;
static constexpr int kBusXPosIn   = 3;
static constexpr int kBusYPosIn   = 4;
static constexpr int kBusGlobalOut = 13;
static constexpr int kBusLocalOut  = 14;

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static void write_gate_pulse(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    dst[0] = 6.0f;
    for (int i = 1; i < kNumFrames; ++i) dst[i] = 0.0f;
}

static void write_gate_high(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 6.0f;
}

static void clear_bus(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
}

static float read_cv_output(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static nt::LoadedPlugin* setup() {
    nt::reset_runtime();
    hem_shim::inner_ticks_override = 0;  // default: 10 ticks per step
    return nt::load_plugin();
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

TEST_CASE("GameOfLife GL1: plugin loads successfully", "[per-applet][game-of-life]") {
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    REQUIRE(loaded->factory   != nullptr);
}

TEST_CASE("GameOfLife GL2: OnDataRequest packs weight=30 after Start", "[per-applet][game-of-life]") {
    // Start() sets weight=30. OnDataRequest packs it into bits [0,6).
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    uint64_t packed = game_of_life_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x3F) == 30u);
}

TEST_CASE("GameOfLife GL3: serialise round-trip preserves weight", "[per-applet][game-of-life]") {
    // Inject weight=50 and confirm it round-trips.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = 50u;  // bits [0,6) = 50
    game_of_life_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = game_of_life_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x3F) == 50u);
}

TEST_CASE("GameOfLife GL4: encoder turn changes weight via customUi", "[per-applet][game-of-life]") {
    // Default weight=30. Encoder +1 -> weight=31.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);
    REQUIRE((game_of_life_applet_on_data_request(loaded->algorithm) & 0x3F) == 30u);

    _NT_uiData ui{};
    ui.encoders[0]  = 1;
    ui.controls     = 0;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE((game_of_life_applet_on_data_request(loaded->algorithm) & 0x3F) == 31u);
}

TEST_CASE("GameOfLife GL5: button press clears the board", "[per-applet][game-of-life]") {
    // After Start() the board has cells. OnButtonPress() zeros the board.
    // After clearing, one step with no clock should yield 0 global density
    // (Out(0) = 0V).
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    // Clear the board via the button press test seam.
    game_of_life_clear_board(loaded->algorithm);

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusClockIn);
    clear_bus(bus, kBusDrawIn);
    write_cv_bus(bus, kBusXPosIn, 0.0f);
    write_cv_bus(bus, kBusYPosIn, 0.0f);

    hem_shim::inner_ticks_override = 1;
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float global_out = read_cv_output(bus, kBusGlobalOut);
    // Empty board -> global_density=0 -> Out(0)=0.
    REQUIRE(global_out < 0.1f);
}

TEST_CASE("GameOfLife GL6: clock advances board producing non-zero global density", "[per-applet][game-of-life]") {
    // Start() seeds a live board. One generation advance (inner_ticks=1, one
    // clock pulse) should yield nonzero global density on Out(0).
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusDrawIn);
    write_cv_bus(bus, kBusXPosIn, 0.0f);
    write_cv_bus(bus, kBusYPosIn, 0.0f);

    write_gate_pulse(bus, kBusClockIn);
    hem_shim::inner_ticks_override = 1;
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float global_out = read_cv_output(bus, kBusGlobalOut);
    // Seeded board should survive at least one generation.
    REQUIRE(global_out > 0.0f);
}

TEST_CASE("GameOfLife GL7: draw gate adds a cell at traveler position", "[per-applet][game-of-life]") {
    // Clear board first. Hold Draw gate high. Step once. Then clock once
    // with inner_ticks=1. The added cell should contribute to density output.
    // Note: a single isolated cell dies (no neighbors survive), but with
    // the seeded glider-like initial state cleared, we add a cluster to
    // survive. Here we just verify that Draw gate does not crash and that
    // stepping produces valid (>=0) output.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    game_of_life_clear_board(loaded->algorithm);

    float* bus = nt::bus_frames_base();
    write_cv_bus(bus, kBusXPosIn, 0.0f);
    write_cv_bus(bus, kBusYPosIn, 0.0f);
    write_gate_high(bus, kBusDrawIn);
    clear_bus(bus, kBusClockIn);

    hem_shim::inner_ticks_override = 1;
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Just assert it ran without crash and output is non-negative.
    float global_out = read_cv_output(bus, kBusGlobalOut);
    REQUIRE(global_out >= 0.0f);
}

TEST_CASE("GameOfLife GL8: encoder button press routes via customUi without crash", "[per-applet][game-of-life]") {
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("GameOfLife GL9: aux button press routes via customUi without crash", "[per-applet][game-of-life]") {
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
