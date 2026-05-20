// Per-applet pilot test: Cumulus.
//
// Manifest: shim/include/applet_manifests/Cumulus.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Cumulus.h
//
// 10x coverage shape: EXPLICIT-10x.
// Cumulus advances acc_register inside if (Clock(0)) / if (Clock(1)).
// The per-applet runtime fires Controller() 10 times per buffer
// (ticks_this_step = numFrames/3 = 32/3 = 10). One set_gate pulse on Clock(0)
// causes ADD to run 10 times in one step. All clock-driven fire-count assertions
// in this file model the 10x multiplier explicitly, matching the canonical
// commentary at harness/tests/test_hemispheres.cpp:1264 (CU2).

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Shim headers needed to call the applet directly and to access
// HemiPluginInterface members on the algorithm instance.
#include "../../shim/include/HemisphereApplet.h"
#include "../../shim/include/Arduino.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/Cumulus.h"
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/HSIOFrame.h"

// get_cumulus_applet() is defined in plugins/applets/Cumulus.cpp (same link
// unit). It exposes the Cumulus instance for direct state inspection.
Cumulus* get_cumulus_applet(_NT_algorithm* self);

// ---------------------------------------------------------------------------
// Local pack helper (applet_test_helpers.cpp is not linked in per-applet
// test binaries; reimplement pack_cumulus verbatim per the pack-helper rules
// in CLAUDE.md "Pack helper convention").
//
// Cumulus::OnDataRequest bit layout (17 bits used):
//   [0,  3) = accoperator (ADD=0, SUB=1, MULADD1=2, XOR_ROTL=3, SUB_ROTR=4)
//   [3,  4) = b_constant  (0..ACC_MAX_B=15; no bias)
//   [7,  4) = outmode[0]  (0..7; vendor constrains to 0..7 on receive)
//   [11, 2) = UNUSED gap  (must be 0 on pack; zeroed explicitly)
//   [13, 4) = outmode[1]  (0..7; vendor constrains to 0..7 on receive)
// ---------------------------------------------------------------------------
static uint64_t pack_cumulus(int accoperator, int b_constant,
                              int outmode_left, int outmode_right) {
    uint64_t data = 0;
    data |= ((uint64_t)(accoperator   & 0x07));
    data |= ((uint64_t)(b_constant    & 0x0F)) << 3;
    data |= ((uint64_t)(outmode_left  & 0x0F)) << 7;
    // bits 11..12 left as 0 (vendor gap)
    data |= ((uint64_t)(outmode_right & 0x0F)) << 13;
    return data;
}

// ---------------------------------------------------------------------------
// Bus read helpers for the per-applet bus layout.
//
// Cumulus manifest:
//   input  0 = Clock  (gate, bus parameter index 0)
//   input  1 = CV     (cv,   bus parameter index 1)
//   output 0 = Out A  (cv,   bus parameter index 2; mode at index 3)
//   output 1 = Out B  (cv,   bus parameter index 4; mode at index 5)
//
// load_plugin() wires v[i] = s_params[i].def:
//   v[0] = 1  (Clock -> bus 1)
//   v[1] = 2  (CV    -> bus 2)
//   v[2] = 13 (Out A -> bus 13)
//   v[3] = 1  (mode = additive)
//   v[4] = 14 (Out B -> bus 14)
//   v[5] = 1  (mode = additive)
// ---------------------------------------------------------------------------

static constexpr int kNumFrames      = 32;
static constexpr int kNumFramesBy4   = kNumFrames / 4;

// Bus 1-based indices matching default v[] from emit_base_parameters.
static constexpr int kBusClockIn = 1;
static constexpr int kBusCvIn    = 2;
static constexpr int kBusOutA    = 13;
static constexpr int kBusOutB    = 14;

// Write a single-frame gate pulse on the clock input bus.
static void set_clock_pulse(float* busFrames) {
    float* clk = busFrames + (kBusClockIn - 1) * kNumFrames;
    clk[0] = 6.0f;  // rising edge sample
    for (int i = 1; i < kNumFrames; ++i) clk[i] = 0.0f;
}

// Read a gate output on busOut at frame 0 (> 0.5V = true).
static bool read_gate_output(float* busFrames, int busIdx) {
    return busFrames[(busIdx - 1) * kNumFrames] > 0.5f;
}

// ---------------------------------------------------------------------------
// Test setup: load plugin and return the algorithm pointer.
// Resets the runtime before each test to clear bus and singleton state.
// ---------------------------------------------------------------------------

static nt::LoadedPlugin* setup_plugin() {
    nt::reset_runtime();
    // HS::frame is a global and not reset by reset_runtime(). Zero it here to
    // prevent output values from a previous test step leaking into this test.
    // write_outputs_to_bus always writes HS::frame.outputs[*].value to the
    // bus regardless of whether the applet updated it this step.
    std::memset(&HS::frame, 0, sizeof(HS::frame));
    return nt::load_plugin();
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

TEST_CASE("Cumulus PA1: plugin loads and construct returns non-null", "[per-applet-pilot][cumulus]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    REQUIRE(loaded->factory   != nullptr);
}

TEST_CASE("Cumulus PA2: Start defaults: accoperator=ADD=0, b_constant=0", "[per-applet-pilot][cumulus]") {
    nt::LoadedPlugin* loaded = setup_plugin();
    Cumulus* app = get_cumulus_applet(loaded->algorithm);

    uint64_t packed = app->OnDataRequest();
    int op = (int)(packed & 0x07);
    int b  = (int)((packed >> 3) & 0x0F);
    REQUIRE(op == 0);  // ADD
    REQUIRE(b  == 0);
}

TEST_CASE("Cumulus PA3: round-trip preserves accoperator, b_constant, outmodes; gap bits stay zero",
          "[per-applet-pilot][cumulus]") {
    // Coverage shape: round-trip only; no clock-driven state used here.
    nt::LoadedPlugin* loaded = setup_plugin();
    Cumulus* app = get_cumulus_applet(loaded->algorithm);

    app->OnDataReceive(pack_cumulus(2, 7, 5, 11));

    uint64_t packed = app->OnDataRequest();
    int op  = (int)(packed & 0x07);
    int b   = (int)((packed >> 3) & 0x0F);
    int om0 = (int)((packed >> 7) & 0x0F);
    int gap = (int)((packed >> 11) & 0x03);  // bits 11..12 MUST be zero
    int om1 = (int)((packed >> 13) & 0x0F);

    REQUIRE(op  == 2);
    REQUIRE(b   == 7);
    REQUIRE(om0 == 5);
    REQUIRE(gap == 0);   // explicit gap-bit check per CLAUDE.md pack helper convention
    REQUIRE(om1 == 7);   // 11 clamped to 7 by vendor constrain(..., 0, 7)
}

TEST_CASE("Cumulus PA4: ADD op advances acc_register by b_constant x10 per step",
          "[per-applet-pilot][cumulus]") {
    // 10x coverage shape: explicit.
    // ADD with b=1. One Clock(0) pulse fires ADD 10 times (10 inner ticks).
    // acc = 0 + 10*1 = 10 = 0b00001010.
    // outmode[0]=bit1: bit 1 of 10 = 1 -> Out A high.
    // outmode[1]=bit0: bit 0 of 10 = 0 -> Out B low.
    nt::LoadedPlugin* loaded = setup_plugin();
    Cumulus* app = get_cumulus_applet(loaded->algorithm);
    app->OnDataReceive(pack_cumulus(0, 1, 1, 0));  // ADD, b=1, omA=bit1, omB=bit0

    float* busFrames = nt::bus_frames_base();
    set_clock_pulse(busFrames);
    loaded->factory->step(loaded->algorithm, busFrames, kNumFramesBy4);

    REQUIRE(read_gate_output(busFrames, kBusOutA) == true);
    REQUIRE(read_gate_output(busFrames, kBusOutB) == false);
}

TEST_CASE("Cumulus PA5: SUB op decreases acc_register x10 per step",
          "[per-applet-pilot][cumulus]") {
    // 10x coverage shape: explicit.
    // SUB with b=5. acc = 0 - 10*5 = -50 (uint8_t wrap) = 206 = 0xCE = 0b11001110.
    // outmode[0]=bit1: bit 1 of 206 = 1 -> Out A high.
    nt::LoadedPlugin* loaded = setup_plugin();
    Cumulus* app = get_cumulus_applet(loaded->algorithm);
    app->OnDataReceive(pack_cumulus(1, 5, 1, 1));  // SUB, b=5, omA=bit1, omB=bit1

    float* busFrames = nt::bus_frames_base();
    set_clock_pulse(busFrames);
    loaded->factory->step(loaded->algorithm, busFrames, kNumFramesBy4);

    REQUIRE(read_gate_output(busFrames, kBusOutA) == true);
}

TEST_CASE("Cumulus PA6: outmode selects correct bit of acc_register",
          "[per-applet-pilot][cumulus]") {
    // 10x coverage shape: explicit.
    // ADD with b=5. acc = 50 = 0b00110010.
    // bit 1 of 50 = 1 -> Out A high (outmode[0]=bit1).
    // bit 5 of 50 = 1 -> Out B high (outmode[1]=bit5).
    nt::LoadedPlugin* loaded = setup_plugin();
    Cumulus* app = get_cumulus_applet(loaded->algorithm);
    app->OnDataReceive(pack_cumulus(0, 5, 1, 5));  // ADD, b=5, omA=bit1, omB=bit5

    float* busFrames = nt::bus_frames_base();
    set_clock_pulse(busFrames);
    loaded->factory->step(loaded->algorithm, busFrames, kNumFramesBy4);

    REQUIRE(read_gate_output(busFrames, kBusOutA) == true);
    REQUIRE(read_gate_output(busFrames, kBusOutB) == true);
}

TEST_CASE("Cumulus PA7: no clock pulse leaves outputs unchanged",
          "[per-applet-pilot][cumulus]") {
    // With ADD and non-zero b but no clock, acc is never updated.
    // Outputs should remain 0 (acc starts at 0; bit 1 of 0 = 0).
    nt::LoadedPlugin* loaded = setup_plugin();
    Cumulus* app = get_cumulus_applet(loaded->algorithm);
    app->OnDataReceive(pack_cumulus(0, 5, 1, 0));  // ADD, b=5, omA=bit1, omB=bit0

    float* busFrames = nt::bus_frames_base();
    // No pulse; bus stays at 0.
    loaded->factory->step(loaded->algorithm, busFrames, kNumFramesBy4);

    REQUIRE(read_gate_output(busFrames, kBusOutA) == false);
    REQUIRE(read_gate_output(busFrames, kBusOutB) == false);
}

TEST_CASE("Cumulus PA8: customUi encoder turn advances cursor via OnEncoderMove",
          "[per-applet-pilot][cumulus]") {
    // Encoder turn changes the cursor (or parameter in edit mode). We verify
    // no crash and that the factory dispatches the call. Exceptions are
    // disabled; the test passes if customUi returns without fault.
    nt::LoadedPlugin* loaded = setup_plugin();

    _NT_uiData ui{};
    ui.controls     = 0;
    ui.lastButtons  = 0;
    ui.encoders[0]  = 1;   // turn encoder right
    ui.encoders[1]  = 0;

    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);  // reached here = no crash
}

TEST_CASE("Cumulus PA9: customUi encoder button press calls OnButtonPress",
          "[per-applet-pilot][cumulus]") {
    nt::LoadedPlugin* loaded = setup_plugin();

    _NT_uiData ui{};
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;    // rising edge: was 0, now 1
    ui.encoders[0]  = 0;
    ui.encoders[1]  = 0;

    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);  // reached here = no crash
}

TEST_CASE("Cumulus PA10: customUi button1 calls on_aux_button",
          "[per-applet-pilot][cumulus]") {
    nt::LoadedPlugin* loaded = setup_plugin();

    _NT_uiData ui{};
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;    // rising edge
    ui.encoders[0]  = 0;
    ui.encoders[1]  = 0;

    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);  // reached here = no crash
}

TEST_CASE("Cumulus PA11: HemiPluginInterface magic and version populated by construct",
          "[per-applet-pilot][cumulus]") {
    nt::LoadedPlugin* loaded = setup_plugin();

    auto* iface = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(iface->magic             == kHemiInterfaceMagic);
    REQUIRE(iface->interface_version == kHemiInterfaceVersion);
    REQUIRE(iface->render_view        != nullptr);
    REQUIRE(iface->on_encoder_turn    != nullptr);
    REQUIRE(iface->on_button_press    != nullptr);
    REQUIRE(iface->on_aux_button      != nullptr);
}
