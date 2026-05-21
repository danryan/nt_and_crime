// Per-applet test: Pigeons.
//
// Manifest: shim/include/applet_manifests/Pigeons.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Pigeons.h
//
// Pigeons implements a dual-channel Fibonacci-modulo pitch sequencer with
// quantization. Each channel advances a Fibonacci-like sequence modulo a
// configurable value and looks up the result via HS::QuantizerLookup().
//
// 10x ticks-per-step concern: Controller() calls Bump() only inside
// if (Clock(ch)). clocked[ch] stays asserted across all 10 inner ticks per
// buffer step, so Bump() fires 10 times per rising edge per step call.
// Tests avoid asserting specific note values after a clock edge; instead they
// verify serialization round-trips and structural routing.
//
// Bus parameter layout (per emit_base_parameters):
//   v[0]  = input  bus for "Clock 1"   (default 1)
//   v[1]  = input  bus for "Clock 2"   (default 2)
//   v[2]  = input  bus for "Modulo 1"  (default 3)
//   v[3]  = input  bus for "Modulo 2"  (default 4)
//   v[4]  = output bus for "Pitch 1"   (default 13)
//   v[5]  = output mode for "Pitch 1"  (default 1 = replace)
//   v[6]  = output bus for "Pitch 2"   (default 14)
//   v[7]  = output mode for "Pitch 2"  (default 1 = replace)
//
// Default packed state after Start() on LEFT_HEMISPHERE:
//   pigeons[0].val[0]=1  bits[0,6)
//   pigeons[0].val[1]=2  bits[6,6)
//   pigeons[0].mod=7     bits[12,6) packs (mod-1)=6
//   pigeons[1].val[0]=1  bits[18,6)
//   pigeons[1].val[1]=2  bits[24,6)
//   pigeons[1].mod=10    bits[30,6) packs (mod-1)=9
//   qselect[0]=0         bits[36,4)
//   qselect[1]=1         bits[44,4)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

using Catch::Approx;

// Test seams defined in plugins/applets/Pigeons.cpp.
uint64_t pigeons_applet_on_data_request(_NT_algorithm* self);
void     pigeons_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Default output bus numbers from emit_base_parameters (4 inputs -> outputs at 13,14).
static constexpr int kBusPitch1 = 13;
static constexpr int kBusPitch2 = 14;

static float read_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Build expected packed value from field components using the same bit layout
// as Pigeons::OnDataRequest.
static uint64_t pack_pigeons(uint8_t v0_0, uint8_t v0_1, uint8_t mod0,
                              uint8_t v1_0, uint8_t v1_1, uint8_t mod1,
                              uint8_t qs0,  uint8_t qs1) {
    uint64_t d = 0;
    d |= (uint64_t)(v0_0       & 0x3F) <<  0;
    d |= (uint64_t)(v0_1       & 0x3F) <<  6;
    d |= (uint64_t)((mod0 - 1) & 0x3F) << 12;
    d |= (uint64_t)(v1_0       & 0x3F) << 18;
    d |= (uint64_t)(v1_1       & 0x3F) << 24;
    d |= (uint64_t)((mod1 - 1) & 0x3F) << 30;
    d |= (uint64_t)(qs0        & 0x0F) << 36;
    d |= (uint64_t)(qs1        & 0x0F) << 44;
    return d;
}

TEST_CASE("Pigeons PG1: default packed state after Start", "[per-applet][pigeons]") {
    // Verifies Start() default field values via OnDataRequest.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t packed = pigeons_applet_on_data_request(loaded->algorithm);
    uint64_t expected = pack_pigeons(1, 2, 7, 1, 2, 10, 0, 1);
    REQUIRE(packed == expected);
}

TEST_CASE("Pigeons PG2: serialise round-trip preserves all fields", "[per-applet][pigeons]") {
    // Inject a distinctive state and confirm OnDataRequest reproduces it.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_pigeons(5, 8, 13, 3, 11, 20, 2, 3);
    pigeons_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = pigeons_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == state_in);
}

TEST_CASE("Pigeons PG3: step() with no clock does not produce output", "[per-applet][pigeons]") {
    // With all buses zero (no clock edge), Out() is never called so output
    // buses remain 0V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float bus[32 * kNumFrames] = {};
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_bus_last(bus, kBusPitch1) == Approx(0.0f).margin(0.01f));
    REQUIRE(read_bus_last(bus, kBusPitch2) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Pigeons PG4: step() with clock edge changes output", "[per-applet][pigeons]") {
    // A rising edge on bus 1 (Clock 1) causes Bump() to fire (10x) and
    // QuantizerLookup produces a non-zero pitch for non-zero sequence values.
    // We verify that after a clock edge the output on Pitch 1 is non-zero.
    // Exact pitch value is not asserted (10x multiplier makes it
    // implementation-dependent).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Set val[0]=3, val[1]=5, mod=8 so Bump() produces a non-zero sequence.
    uint64_t state_in = pack_pigeons(3, 5, 8, 1, 2, 7, 0, 1);
    pigeons_applet_on_data_receive(loaded->algorithm, state_in);

    float bus[32 * kNumFrames] = {};
    // Write a gate high on bus 1 (Clock 1) for all frames.
    float* clk1 = bus + (1 - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) clk1[i] = 5.0f;

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Output should be non-zero (sequence bumped and looked up).
    float out1 = read_bus_last(bus, kBusPitch1);
    REQUIRE(out1 != Approx(0.0f).margin(0.001f));
}

TEST_CASE("Pigeons PG5: encoder turn advances cursor via customUi", "[per-applet][pigeons]") {
    // Encoder turn +1 moves cursor from CHAN1_V1(0) to CHAN1_V2(1).
    // After entering edit mode (button press) and turning, val[0] changes.
    // This test stays in non-edit mode: cursor moves, state unchanged.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t before = pigeons_applet_on_data_request(loaded->algorithm);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Cursor move in non-edit mode does not change packed state.
    uint64_t after = pigeons_applet_on_data_request(loaded->algorithm);
    REQUIRE(before == after);
}

TEST_CASE("Pigeons PG6: encoder button enters edit mode, turn modifies val[0]", "[per-applet][pigeons]") {
    // Press encoder button (rising edge) to enter edit mode, then turn +1.
    // Cursor starts at CHAN1_V1; edit mode increments pigeons[0].val[0] from 1 to 2.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Press encoder button to enter edit mode.
    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Turn encoder +1 to increment val[0].
    ui.controls    = 0;
    ui.lastButtons = kNT_encoderButtonL;
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t packed = pigeons_applet_on_data_request(loaded->algorithm);
    uint8_t val0_0 = packed & 0x3F;
    REQUIRE(val0_0 == 2u);
}

TEST_CASE("Pigeons PG7: aux button routes to AuxButton without crash", "[per-applet][pigeons]") {
    // AuxButton calls CancelEdit(); must not crash when not in edit mode.
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
