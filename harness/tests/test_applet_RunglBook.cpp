// Per-applet test: RunglBook.
//
// Manifest: shim/include/applet_manifests/RunglBook.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/RunglBook.h
//
// RunglBook maintains an 8-bit shift register (reg). On each Clock (Digital 1)
// rising edge it either shifts CV-derived data in (Digital 2 low) or rotates
// left (Digital 2 / Freeze high). Out(0) = Proportion(reg & 0x07, 7, MAX_CV);
// Out(1) = Proportion((reg >> 5) & 0x07, 7, MAX_CV).
//
// 10x clock multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer.
//   A single rising Clock edge asserts HS::frame.clocked[0] across all 10
//   inner ticks, advancing the shift register 10 times per step() call.
//   Tests use hem_shim::inner_ticks_override=1 for deterministic single-shift
//   behaviour. Outputs are written directly inside if(Clock(0)), so no ADC
//   lag cycle is required - a single step() with override=1 both shifts and
//   emits the output.
//
// Bus parameter layout (emit_base_parameters, 4 inputs + 2 outputs):
//   v[0]  = 1   Clock    bus selector (gate, default bus 1)
//   v[1]  = 2   Freeze   bus selector (gate, default bus 2)
//   v[2]  = 3   Signal   bus selector (cv,   default bus 3)
//   v[3]  = 4   Thresh   bus selector (cv,   default bus 4)
//   v[4]  = 13  Rungle   bus selector (cv,   default bus 13)
//   v[5]  = 1   Rungle   mode (replace)
//   v[6]  = 14  Alt      bus selector (cv,   default bus 14)
//   v[7]  = 1   Alt      mode (replace)
//
// OnDataRequest packs:
//   bits [0,16) = threshold (16 bits)
//
// Start() initialises threshold = ONE_OCTAVE * 2 = 3072 hem units (~2V).

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

using Catch::Approx;

// Test seams defined in plugins/applets/RunglBook.cpp.
uint64_t runglbook_applet_on_data_request(_NT_algorithm* self);
void     runglbook_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// inner_ticks_override: set to 1 to run exactly one Controller() tick per
// step() call, making shift-register advancement tests deterministic.
namespace hem_shim { extern int inner_ticks_override; }

namespace {

// Default bus assignments from emit_base_parameters for RunglBook.
constexpr int kBusClock   = 1;   // v[0] default - Clock input (gate)
constexpr int kBusFreeze  = 2;   // v[1] default - Freeze input (gate)
constexpr int kBusSignal  = 3;   // v[2] default - Signal CV input
constexpr int kBusThresh  = 4;   // v[3] default - Threshold mod CV input
constexpr int kBusRungle  = 13;  // v[4] default - Rungle output
constexpr int kBusAlt     = 14;  // v[6] default - Alt output

constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

// Vendor constants (hem units).
constexpr int kOneOctave   = 1536;
constexpr int kMaxCV       = 9216;  // HEMISPHERE_MAX_CV

void clear_all_buses(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a constant CV voltage across all frames of a 1-based bus.
void write_cv_bus(float* bus, int bus_1based, float volts) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a single rising-edge gate pulse at frame 0 on a 1-based bus.
void write_gate_pulse(float* bus, int bus_1based) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    dst[0] = 6.0f;
    for (int i = 1; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Write a sustained gate high across all frames of a 1-based bus.
void write_gate_high(float* bus, int bus_1based) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 6.0f;
}

// Clear a single bus to 0V.
void clear_one_bus(float* bus, int bus_1based) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Returns the last-frame value (in volts) of a 1-based output bus.
float read_output_volts(const float* bus, int bus_1based) {
    return bus[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Encode a threshold value into a data word for OnDataReceive.
// threshold is a raw hem-unit value (16-bit field at bits [0,16)).
uint64_t encode_threshold(uint16_t threshold) {
    return (uint64_t)threshold;
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
    clear_all_buses(bus);
    // One warmup step to settle BaseStart state (reg=0 after Start).
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_all_buses(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

// Run one deterministic single-shift step.
// Writes clock pulse, sets inner_ticks_override=1, calls step(), clears clock.
void single_step(Setup& s) {
    write_gate_pulse(s.bus, kBusClock);
    hem_shim::inner_ticks_override = 1;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    clear_one_bus(s.bus, kBusClock);
}

}  // namespace

// ---------------------------------------------------------------------------
// RB1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB1: pluginEntry returns factory with correct guid", "[per-applet][runglbook]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','R','g');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "RunglBook");
}

// ---------------------------------------------------------------------------
// RB2: construct populates HemiPluginInterface fields correctly.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB2: construct populates HemiPluginInterface magic and version", "[per-applet][runglbook]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* p = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(p->magic             == kHemiInterfaceMagic);
    REQUIRE(p->interface_version == kHemiInterfaceVersion);
    REQUIRE(p->render_view             != nullptr);
    REQUIRE(p->on_encoder_turn         != nullptr);
    REQUIRE(p->on_encoder_turn_shifted != nullptr);
    REQUIRE(p->on_button_press         != nullptr);
    REQUIRE(p->on_aux_button           != nullptr);
}

// ---------------------------------------------------------------------------
// RB3: OnDataRequest packs threshold = ONE_OCTAVE*2 = 3072 after Start.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB3: OnDataRequest packs threshold=3072 after Start", "[per-applet][runglbook]") {
    // Vendor Start() sets threshold = ONE_OCTAVE * 2 = 1536 * 2 = 3072.
    // OnDataRequest packs threshold into bits [0,16).
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t packed = runglbook_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFFFu) == 3072u);
}

// ---------------------------------------------------------------------------
// RB4: serialise round-trip preserves threshold.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB4: serialise round-trip preserves threshold=5000", "[per-applet][runglbook]") {
    // Inject threshold=5000 and confirm it round-trips through
    // OnDataReceive/OnDataRequest.
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    runglbook_applet_on_data_receive(loaded->algorithm, encode_threshold(5000u));
    uint64_t packed = runglbook_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFFFFu) == 5000u);
}

// ---------------------------------------------------------------------------
// RB5: above-threshold Signal shifts 1 into reg -> Rungle = MAX_CV.
//
// With threshold=3072 (~2V) and Signal=4V (6144 hem > 3072): b0=1.
// Starting from reg=0, one shift gives reg=0x01 -> reg & 0x07 = 1.
// Out(0) = Proportion(1, 7, 9216) = 1316 hem (~0.857V).
// After 7 more shifts (8 total): reg=0xFF -> reg & 0x07 = 7.
// Out(0) = Proportion(7, 7, 9216) = 9216 hem = 6V.
// Use inner_ticks_override=1 and 8 sequential steps to fill register.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB5: 8 above-threshold shifts fill reg 0x07 bits -> Rungle = 6V", "[per-applet][runglbook]") {
    // Vendor RunglBook.h:42-54: on Clock(0) with Freeze low and In(0) > threshold:
    //   b0 = 1; reg = (reg << 1) | 1
    //   Out(0) = Proportion(reg & 0x07, 0x07, 9216)
    // After 8 one-bit shifts from reg=0: reg=0xFF; reg & 0x07 = 7.
    // Proportion(7, 7, 9216) = 9216 hem = 6V.
    auto s = make_setup();

    write_cv_bus(s.bus, kBusSignal, 4.0f);  // 4V > threshold (2V): b0=1

    for (int i = 0; i < 8; ++i) {
        single_step(s);
    }

    float rungle = read_output_volts(s.bus, kBusRungle);
    REQUIRE(rungle == Approx(6.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// RB6: below-threshold Signal shifts 0s -> Rungle = 0V.
//
// With threshold=3072 (~2V) and Signal=1V (1536 hem < 3072): b0=0.
// Starting from reg=0, one shift gives reg=0 -> reg & 0x07 = 0.
// Out(0) = Proportion(0, 7, 9216) = 0. Any number of zero-shifts from 0
// keep reg=0.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB6: below-threshold Signal shifts 0s -> Rungle = 0V", "[per-applet][runglbook]") {
    auto s = make_setup();

    write_cv_bus(s.bus, kBusSignal, 1.0f);  // 1V < threshold (2V): b0=0

    single_step(s);

    float rungle = read_output_volts(s.bus, kBusRungle);
    REQUIRE(rungle == Approx(0.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// RB7: Alt output reflects upper bits ((reg >> 5) & 0x07).
//
// After 8 above-threshold shifts: reg=0xFF.
// (reg >> 5) & 0x07 = 7. Out(1) = Proportion(7, 7, 9216) = 6V.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB7: 8 above-threshold shifts -> Alt output = 6V", "[per-applet][runglbook]") {
    auto s = make_setup();

    write_cv_bus(s.bus, kBusSignal, 4.0f);  // 4V > threshold

    for (int i = 0; i < 8; ++i) {
        single_step(s);
    }

    float alt = read_output_volts(s.bus, kBusAlt);
    REQUIRE(alt == Approx(6.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// RB8: Freeze (Gate 1 high) rotates register, preserving all-ones.
//
// Vendor RunglBook.h:43-45: when Gate(1) high:
//   reg = (reg << 1) | ((reg >> 7) & 0x01)  -- left rotate
// Setup: fill reg=0xFF via 8 above-threshold shifts.
// Then assert Freeze and below-threshold Signal. Without freeze: 1 zero-shift
// clears bit 0 -> reg & 0x07 could be anything. With freeze: rotation of
// 0xFF -> 0xFF (all bits preserved). Out(0) stays at 6V.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB8: Freeze rotates 0xFF register, Rungle stays 6V", "[per-applet][runglbook]") {
    auto s = make_setup();

    // Fill reg=0xFF with 8 above-threshold shifts.
    write_cv_bus(s.bus, kBusSignal, 4.0f);
    for (int i = 0; i < 8; ++i) {
        single_step(s);
    }

    // Engage Freeze with below-threshold Signal.
    // Without freeze: zero-shift would eventually clear low bits.
    // With freeze: rotation of 0xFF leaves reg=0xFF -> Rungle=6V.
    clear_one_bus(s.bus, kBusSignal);
    write_cv_bus(s.bus, kBusSignal, 1.0f);  // below threshold
    write_gate_high(s.bus, kBusFreeze);     // Freeze asserted

    single_step(s);

    float rungle = read_output_volts(s.bus, kBusRungle);
    REQUIRE(rungle == Approx(6.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// RB9: encoder turn adjusts threshold via customUi.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB9: encoder turn adjusts threshold via customUi", "[per-applet][runglbook]") {
    // Vendor OnEncoderMove: threshold += direction * 128, clamped [ONE_OCTAVE, 5*ONE_OCTAVE].
    // Default threshold=3072. After +1 turn: 3072 + 128 = 3200.
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    REQUIRE((runglbook_applet_on_data_request(alg) & 0xFFFFu) == 3072u);

    _NT_uiData ui{};
    ui.encoders[0]  = 1;
    ui.controls     = 0;
    ui.lastButtons  = 0;
    loaded->factory->customUi(alg, ui);

    REQUIRE((runglbook_applet_on_data_request(alg) & 0xFFFFu) == 3200u);
}

// ---------------------------------------------------------------------------
// RB10: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB10: hasCustomUi returns expected bitmask", "[per-applet][runglbook]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// RB11: encoder button press does not crash (OnButtonPress is a no-op).
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB11: customUi encoder button press does not crash", "[per-applet][runglbook]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// RB12: button1 press routes on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("RunglBook RB12: customUi button1 press routes on_aux_button", "[per-applet][runglbook]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
