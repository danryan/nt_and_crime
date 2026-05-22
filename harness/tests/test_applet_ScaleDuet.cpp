// Per-applet test: ScaleDuet.
//
// Manifest: shim/include/applet_manifests/ScaleDuet.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ScaleDuet.h
//
// 10x ticks-per-step note: ScaleDuet's Controller() uses Clock(0) to
// trigger StartADCLag / EndOfADCLag. Bus-level pitch-change assertions
// after a single clock edge are unreliable because the 10x inner tick
// multiplier refires the rising edge state. Tests here cover default
// state serialisation, round-trip, and UI routing. Continuous-mode
// pitch-quantization is tested via steady-state CV with no clock edge
// (continuous=1 is the initial default, so no gate needed).
//
// OnDataRequest byte layout (vendor ScaleDuet.h):
//   bits [0..11]  = mask[0]  (12-bit chromatic mask for scale 1; default 0xFFF)
//   bits [12..23] = mask[1]  (12-bit chromatic mask for scale 2; default 0xFFF)
//
// Bus parameter layout (per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Clock"   (default 1)
//   v[1]  = input  bus for "Scale2"  (default 2)
//   v[2]  = input  bus for "CV"      (default 3)
//   v[3]  = input  bus for "Unclock" (default 4)
//   v[4]  = output bus for "Pitch"   (default 13)
//   v[5]  = output mode for "Pitch"  (default 1 = replace)
//   v[6]  = output bus for "Trig"    (default 14)
//   v[7]  = output mode for "Trig"   (default 1 = replace)
//
// Vendor notes:
//   - continuous=1 at Start: applet passes CV through quantizer every tick.
//   - Chromatic semitone mask: all 12 bits set = all notes allowed.
//   - quant.Process(In(0), 0, 0) -> quantized pitch in hem units.
//   - Out(0, q_pitch): pitch bus driven each tick in continuous mode.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/ScaleDuet.cpp.
uint64_t scaleduet_applet_on_data_request(_NT_algorithm* self);
void     scaleduet_applet_on_data_receive(_NT_algorithm* self, uint64_t data);
uint16_t scaleduet_applet_get_mask(_NT_algorithm* self, int scale);

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Default bus indices (from manifest order).
static constexpr int kBusClockIn  = 1;   // v[0]
static constexpr int kBusScale2   = 2;   // v[1]
static constexpr int kBusCV       = 3;   // v[2]
static constexpr int kBusUnlock   = 4;   // v[3]
static constexpr int kBusPitch    = 13;  // v[4]
static constexpr int kBusTrig     = 14;  // v[6]

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static float read_cv_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static bool read_gate_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)] > 0.5f;
}

static void clear_all_buses(float* busFrames) {
    for (int bus : {kBusClockIn, kBusScale2, kBusCV, kBusUnlock, kBusPitch, kBusTrig}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Pack ScaleDuet serialised state matching vendor OnDataRequest layout.
static uint64_t pack_scaleduet(uint16_t mask0, uint16_t mask1) {
    uint64_t d = 0;
    d |= static_cast<uint64_t>(mask0 & 0xFFF);
    d |= static_cast<uint64_t>(mask1 & 0xFFF) << 12;
    return d;
}

TEST_CASE("ScaleDuet SD1: OnDataRequest encodes default state after Start", "[scaleduet]") {
    // Vendor Start(): mask[0]=0xffff, mask[1]=0xffff. Only 12 bits stored.
    // Expected: bits [0..11]=0xFFF, bits [12..23]=0xFFF.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = scaleduet_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFF)         == 0xFFFu);  // mask[0] all 12 bits set
    REQUIRE(((packed >> 12) & 0xFFF) == 0xFFFu);  // mask[1] all 12 bits set
}

TEST_CASE("ScaleDuet SD2: serialise round-trip preserves both masks", "[scaleduet]") {
    // Inject mask[0]=0b101010101010 (pentatonic-ish), mask[1]=0b010101010101.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    const uint16_t m0 = 0b101010101010u;
    const uint16_t m1 = 0b010101010101u;
    uint64_t state_in = pack_scaleduet(m0, m1);
    scaleduet_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = scaleduet_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFF)         == m0);
    REQUIRE(((packed >> 12) & 0xFFF) == m1);
}

TEST_CASE("ScaleDuet SD3: round-trip of all-zeros mask is stable", "[scaleduet]") {
    // Edge case: mask[0]=0 (no notes). OnDataReceive then OnDataRequest must
    // return the same 0 value.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_scaleduet(0, 0xFFF);
    scaleduet_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = scaleduet_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFF)         == 0u);
    REQUIRE(((packed >> 12) & 0xFFF) == 0xFFFu);
}

TEST_CASE("ScaleDuet SD4: continuous mode drives pitch output from CV input", "[scaleduet]") {
    // continuous=1 at Start, so pitch is quantized and output every tick.
    // With all notes enabled (default mask), CV=2V passes through to pitch bus.
    // We check the pitch bus is non-zero (quantizer fires with chromatic mask).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);
    write_cv_bus(bus, kBusCV, 2.0f);  // 2V input

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // With all chromatic notes enabled, quantized pitch for 2V should be
    // non-zero (2V = 12288 hem, well above zero).
    float pitch = read_cv_bus_last(bus, kBusPitch);
    REQUIRE(pitch > 0.5f);
}

TEST_CASE("ScaleDuet SD5: zero CV input gives near-zero pitch output", "[scaleduet]") {
    // 0V input: quantized pitch in chromatic scale lands on C (0V = 0 hem).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);
    write_cv_bus(bus, kBusCV, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float pitch = read_cv_bus_last(bus, kBusPitch);
    REQUIRE(pitch >= -0.1f);
    REQUIRE(pitch <= 0.1f);
}

TEST_CASE("ScaleDuet SD6: encoder turn advances cursor via customUi", "[scaleduet]") {
    // OnEncoderMove(1) increments cursor from 0. mask state should be unchanged.
    // This confirms routing does not crash and the default state holds.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 1;
    ui.controls     = 0;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // State unchanged after cursor move.
    uint64_t packed = scaleduet_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFF)         == 0xFFFu);
    REQUIRE(((packed >> 12) & 0xFFF) == 0xFFFu);
}

TEST_CASE("ScaleDuet SD7: encoder button press toggles mask bit via customUi", "[scaleduet]") {
    // OnButtonPress() toggles mask bit at cursor position.
    // Default cursor=0, scale=0, bit=0 (C). mask[0] bit0 goes from 1 to 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Confirm bit 0 is set in default mask[0].
    REQUIRE((scaleduet_applet_get_mask(loaded->algorithm, 0) & 0x001) == 1u);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);

    // Bit 0 of mask[0] should now be 0 (toggled off).
    REQUIRE((scaleduet_applet_get_mask(loaded->algorithm, 0) & 0x001) == 0u);
}

TEST_CASE("ScaleDuet SD8: button1 in standalone customUi has no effect (Q5)", "[scaleduet]") {
    // Q5: standalone per-applet customUi no longer routes button1 to
    // on_aux_button. Mask bit stays at its initial value across a
    // button1 rising-edge customUi call.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    REQUIRE((scaleduet_applet_get_mask(loaded->algorithm, 0) & 0x001) == 1u);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE((scaleduet_applet_get_mask(loaded->algorithm, 0) & 0x001) == 1u);
}
