// Per-applet test: ShiftGate.
//
// Manifest: shim/include/applet_manifests/ShiftGate.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ShiftGate.h
//
// ShiftGate maintains two 16-bit shift registers. On each Clock (Digital 1)
// rising edge it shifts the register left. The incoming data bit XORs with
// the high bit that was shifted out; Digital 2 (Freeze) holds the register
// frozen. Each channel's output is either a gate (bit 0 of reg) or a trigger
// (ClockOut when bit 0 is set), selectable per channel.
//
// 10x clock multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer.
//   A single rising Clock edge asserts HS::frame.clocked[0] across all 10
//   inner ticks, so the register shifts 10 times per step() call by default.
//   Tests that need deterministic single-shift behaviour use
//   hem_shim::inner_ticks_override = 1.
//
// Coverage shape: SHAPE 2 (round-trip + state injection; limited bus-level
//   fire-count assertions). Single-shift tests use inner_ticks_override=1.
//
// Bus parameter layout (emit_base_parameters, 4 inputs + 2 outputs):
//   v[0]  = 1   Clock    bus selector (gate, default bus 1)
//   v[1]  = 2   Freeze   bus selector (gate, default bus 2)
//   v[2]  = 3   Flip0 CV bus selector (cv,   default bus 3)
//   v[3]  = 4   Flip1 CV bus selector (cv,   default bus 4)
//   v[4]  = 13  Out 1    bus selector (gate, default bus 13)
//   v[5]  = 1   Out 1    mode (replace)
//   v[6]  = 14  Out 2    bus selector (gate, default bus 14)
//   v[7]  = 1   Out 2    mode (replace)
//
// OnDataRequest packs:
//   bits [0,4)   = length[0] - 1  (4 bits, 1-16 stored as 0-15)
//   bits [4,8)   = length[1] - 1  (4 bits)
//   bits [8,9)   = trigger[0]     (1 bit)
//   bits [9,10)  = trigger[1]     (1 bit)
//   bits [16,32) = reg[0]         (16 bits)
//
// Start() initialises length[0]=4, length[1]=4, trigger[0]=0, trigger[1]=1,
//   reg[ch] = random(0, 0xffff).

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Test seams defined in plugins/applets/ShiftGate.cpp.
uint64_t shiftgate_applet_on_data_request(_NT_algorithm* self);
void     shiftgate_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// inner_ticks_override: set to 1 to run exactly one Controller() tick per
// step() call, making shift-register advancement tests deterministic.
namespace hem_shim { extern int inner_ticks_override; }

namespace {

constexpr int kBusClockIn  = 1;   // v[0] default - Clock input (gate)
constexpr int kBusFreezeIn = 2;   // v[1] default - Freeze input (gate)
constexpr int kBusFlip0CV  = 3;   // v[2] default - Flip0 CV input
constexpr int kBusFlip1CV  = 4;   // v[3] default - Flip1 CV input
constexpr int kBusOut1     = 13;  // v[4] default - Out 1 (gate/trigger)
constexpr int kBusOut2     = 14;  // v[6] default - Out 2 (gate/trigger)
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

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

// Returns true if any frame on a 1-based bus exceeds gate threshold.
bool any_gate_high(const float* bus, int bus_1based) {
    const float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) {
        if (slice[i] > 0.5f) return true;
    }
    return false;
}

// Encode length and trigger settings into a data word (no reg bits set).
// length1 and length2 are 1-indexed (1-16).
uint64_t encode_settings(int length0, int length1, bool trigger0, bool trigger1) {
    uint64_t d = 0;
    d |= (uint64_t)((length0 - 1) & 0xF) << 0;
    d |= (uint64_t)((length1 - 1) & 0xF) << 4;
    d |= (uint64_t)(trigger0 ? 1 : 0) << 8;
    d |= (uint64_t)(trigger1 ? 1 : 0) << 9;
    return d;
}

// Inject reg[0]=known_reg along with settings and clear reg[1].
uint64_t encode_with_reg0(int length0, int length1, bool trigger0, bool trigger1, uint16_t reg0) {
    uint64_t d = encode_settings(length0, length1, trigger0, trigger1);
    d |= (uint64_t)reg0 << 16;
    return d;
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
    // One warmup step to settle BaseStart state.
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_all_buses(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// SG1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG1: pluginEntry returns factory with correct guid", "[per-applet][shiftgate]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','S','g');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "ShiftGate");
}

// ---------------------------------------------------------------------------
// SG2: construct populates HemiPluginInterface fields correctly.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG2: construct populates HemiPluginInterface magic and version", "[per-applet][shiftgate]") {
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
// SG3: OnDataRequest packs length[0]=4 (stored as 3) after Start.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG3: OnDataRequest packs length[0]=4 after Start", "[per-applet][shiftgate]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t packed = shiftgate_applet_on_data_request(loaded->algorithm);
    // length[0] = 4, stored as 4-1 = 3 in bits [0,4).
    REQUIRE((packed & 0xF) == 3u);
    // length[1] = 4, stored as 3 in bits [4,8).
    REQUIRE(((packed >> 4) & 0xF) == 3u);
}

// ---------------------------------------------------------------------------
// SG4: serialise round-trip preserves length and trigger settings.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG4: serialise round-trip preserves length and trigger settings", "[per-applet][shiftgate]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Set length[0]=8, length[1]=12, trigger[0]=true, trigger[1]=false, reg[0]=0xABCD.
    uint64_t state_in = encode_with_reg0(8, 12, true, false, 0xABCDu);
    shiftgate_applet_on_data_receive(alg, state_in);

    uint32_t lo = (uint32_t)(shiftgate_applet_on_data_request(alg) & 0xFFFFFFFFu);
    uint32_t hi = 0u;

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    bool ok = loaded->factory->deserialise(alg, *parse);
    REQUIRE(ok);

    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    loaded->factory->serialise(alg, *stream);
    const std::string& out = stream->buffer();

    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);

    // bits [0,4) = length[0]-1 = 7
    REQUIRE((rt_lo & 0xFu) == 7u);
    // bits [4,8) = length[1]-1 = 11
    REQUIRE(((rt_lo >> 4) & 0xFu) == 11u);
    // bit [8] = trigger[0] = 1
    REQUIRE(((rt_lo >> 8) & 0x1u) == 1u);
    // bit [9] = trigger[1] = 0
    REQUIRE(((rt_lo >> 9) & 0x1u) == 0u);
}

// ---------------------------------------------------------------------------
// SG5: reg[0]=0x0001 in gate mode drives Out1 high after a clock cycle.
//
// The vendor Controller() only writes outputs inside EndOfADCLag(), which
// fires HEMISPHERE_ADC_LAG=96 ticks after StartADCLag() is called on a
// Clock edge. To get a single deterministic shift plus the full ADC lag
// cycle without retriggering, use two step() calls:
//
//   Step 1 (inner_ticks_override=1, clock pulse): fires Clock(0) once,
//     calls StartADCLag(), sets countdown=96.
//   Clear clock bus.
//   Step 2 (inner_ticks_override=96, no clock): countdown decrements 96
//     times from 96 to 0; EndOfADCLag() fires on the 96th tick and the
//     applet writes its outputs.
//
// With reg[0]=0x0001 and gate mode, bit0=1 so GateOut(0,1) is called.
// After the shift triggered on step 1 (length=4, bit3=0, data=0, last=0):
//   reg[0] = (0x0001 << 1) + 0 = 0x0002; bit0=0.
// So Out1 goes LOW after the clock shift. To verify gate-high behaviour we
// need reg[0] to have bit0=1 after the shift. Use reg[0]=0x8001 (bit15=1,
// bit0=1). With length=16: bit(15)=1=last; data=0; last=(0!=1)=1;
//   reg = (0x8001 << 1) + 1 -> lower 16 bits only.
//   (0x10002 & 0xFFFF) + 1 = 0x0003; bit0=1. Out1 stays high.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG5: reg[0]=0x8001 length=16 gate mode drives Out1 high after clock", "[per-applet][shiftgate]") {
    auto s = make_setup();

    // reg[0]=0x8001, gate mode, length=16: bit15=1 (high feedback), bit0=1.
    uint64_t state_in = encode_with_reg0(16, 4, false, false, 0x8001u);
    shiftgate_applet_on_data_receive(s.alg, state_in);

    // Step 1: single clock tick to start the ADC lag cycle.
    write_gate_pulse(s.bus, kBusClockIn);
    hem_shim::inner_ticks_override = 1;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clear clock bus so the edge detector does not refire.
    clear_one_bus(s.bus, kBusClockIn);
    clear_one_bus(s.bus, kBusOut1);

    // Step 2: 96 ticks with no clock to exhaust the ADC lag and emit output.
    hem_shim::inner_ticks_override = 96;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After shift: bit0 of new reg should be 1. Out1 gate should be high.
    REQUIRE(any_gate_high(s.bus, kBusOut1));
}

// ---------------------------------------------------------------------------
// SG6: reg[0]=0x0002 in gate mode keeps Out1 low after clock cycle.
//
// After one shift of 0x0002 with length=4 and CV=0:
//   bit(length-1) = bit3 of 0x0002 = 0; data=0; last=(0!=0)=0.
//   reg = (0x0002 << 1) + 0 = 0x0004; bit0=0. Out1 is low.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG6: reg[0]=0x0002 gate mode Out1 low after clock cycle", "[per-applet][shiftgate]") {
    auto s = make_setup();

    // reg[0]=0x0002, gate mode, length=4.
    uint64_t state_in = encode_with_reg0(4, 4, false, false, 0x0002u);
    shiftgate_applet_on_data_receive(s.alg, state_in);

    // Step 1: single clock tick.
    write_gate_pulse(s.bus, kBusClockIn);
    hem_shim::inner_ticks_override = 1;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clear bus to avoid retriggering.
    clear_one_bus(s.bus, kBusClockIn);
    clear_one_bus(s.bus, kBusOut1);

    // Step 2: exhaust ADC lag (96 ticks), no clock.
    hem_shim::inner_ticks_override = 96;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After shift: bit0=0. Out1 is low.
    REQUIRE_FALSE(any_gate_high(s.bus, kBusOut1));
}

// ---------------------------------------------------------------------------
// SG7: clock shifts reg[0]=0x0002 -> bit 0 cleared, Out1 low after lag.
//
// After one shift of 0x0002 with length=4, CV=0:
//   bit(length-1) = bit3 of 0x0002 = 0; data=0; last=(0!=0)=0.
//   reg[0] = (0x0002 << 1) + 0 = 0x0004; bit0=0. Out1 is low.
//
// Test uses two-step ADC lag pattern (see SG5 comment for protocol).
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG7: single clock tick shifts reg[0]=0x0002 -> Out1 low after lag", "[per-applet][shiftgate]") {
    auto s = make_setup();

    // reg[0]=0x0002, gate mode, length=4.
    uint64_t state_in = encode_with_reg0(4, 4, false, false, 0x0002u);
    shiftgate_applet_on_data_receive(s.alg, state_in);

    // Step 1: single clock tick to trigger the shift and start ADC lag.
    write_gate_pulse(s.bus, kBusClockIn);
    hem_shim::inner_ticks_override = 1;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clear bus to prevent clock retriggering.
    clear_one_bus(s.bus, kBusClockIn);
    clear_one_bus(s.bus, kBusOut1);

    // Step 2: exhaust ADC lag (96 ticks, no clock) to emit the output.
    hem_shim::inner_ticks_override = 96;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After shift of 0x0002 -> 0x0004: bit0=0. Out1 is low.
    REQUIRE_FALSE(any_gate_high(s.bus, kBusOut1));
}

// ---------------------------------------------------------------------------
// SG8: clock shifts reg[0]=0x0008 with length=4 -> wraps bit 3 to Out1 high.
//
// With length=4 and CV=0:
//   bit(length-1) = bit3 of 0x0008 = 1; data=0; last=(0!=1)=1.
//   reg[0] = (0x0008 << 1) + 1 = 0x0011; bit0=1. Out1 goes high.
//
// Test uses two-step ADC lag pattern (see SG5 comment for protocol).
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG8: clock shifts reg[0]=0x0008 with length=4 wraps bit into Out1 high", "[per-applet][shiftgate]") {
    auto s = make_setup();

    // reg[0]=0x0008, gate mode, length=4. bit3=1, bits 0-2 and 4+ = 0.
    uint64_t state_in = encode_with_reg0(4, 4, false, false, 0x0008u);
    shiftgate_applet_on_data_receive(s.alg, state_in);

    // Step 1: single clock tick to trigger shift and start ADC lag.
    write_gate_pulse(s.bus, kBusClockIn);
    hem_shim::inner_ticks_override = 1;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clear bus to prevent clock retriggering.
    clear_one_bus(s.bus, kBusClockIn);
    clear_one_bus(s.bus, kBusOut1);

    // Step 2: exhaust ADC lag to emit the output.
    hem_shim::inner_ticks_override = 96;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // bit3 wrapped into bit0 -> Out1 is high.
    REQUIRE(any_gate_high(s.bus, kBusOut1));
}

// ---------------------------------------------------------------------------
// SG9: Freeze gate suppresses data injection and keeps Out1 low.
//
// With Freeze (Clock(1)) asserted, the "if (!Clock(1))" branch is skipped
// so CV data is not injected into the register; only bit(length-1) feeds back.
//
// Scenario: reg[0]=0x0001, length=16, CV0=4V (>3V -> data=1), Freeze on.
//   Without freeze: last=bit15=0; data=1; last=(1!=0)=1; reg=(0x0002)+1=0x0003; bit0=1.
//   With freeze:    last=bit15=0; data skipped; last=0;   reg=(0x0002)+0=0x0002; bit0=0.
//
// So Freeze causes Out1 to be LOW despite the high CV input.
// Test uses two-step ADC lag pattern (see SG5 comment for protocol).
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG9: Freeze with high CV keeps Out1 low by blocking data injection", "[per-applet][shiftgate]") {
    auto s = make_setup();

    // reg[0]=0x0001, gate mode, length=16. bit0=1, bit15=0.
    uint64_t state_in = encode_with_reg0(16, 4, false, false, 0x0001u);
    shiftgate_applet_on_data_receive(s.alg, state_in);

    // Step 1: Clock + Freeze + CV0=4V (data would be 1 without freeze).
    write_gate_pulse(s.bus, kBusClockIn);
    write_gate_high(s.bus, kBusFreezeIn);  // Freeze asserted
    write_cv_bus(s.bus, kBusFlip0CV, 4.0f); // 4V > 3V -> data=1 if unfrozen
    hem_shim::inner_ticks_override = 1;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clear input buses to avoid retriggering.
    clear_one_bus(s.bus, kBusClockIn);
    clear_one_bus(s.bus, kBusFreezeIn);
    clear_one_bus(s.bus, kBusFlip0CV);
    clear_one_bus(s.bus, kBusOut1);

    // Step 2: exhaust ADC lag (96 ticks, no clock) to emit output.
    hem_shim::inner_ticks_override = 96;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With freeze: last=0 (bit15=0, data skipped), reg shifts to 0x0002; bit0=0.
    // Out1 is LOW despite the high CV.
    REQUIRE_FALSE(any_gate_high(s.bus, kBusOut1));
}

// ---------------------------------------------------------------------------
// SG10: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG10: hasCustomUi returns expected bitmask", "[per-applet][shiftgate]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL | kNT_button1));
}

// ---------------------------------------------------------------------------
// SG11: encoder turn adjusts cursor/length via customUi.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG11: customUi encoder turn adjusts length[0] in edit mode", "[per-applet][shiftgate]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Default length[0]=4 (stored as 3 in bits [0,4)).
    REQUIRE((shiftgate_applet_on_data_request(alg) & 0xFu) == 3u);

    // Enter edit mode with encoder button press.
    _NT_uiData ui_btn{};
    ui_btn.controls     = kNT_encoderButtonL;
    ui_btn.lastButtons  = 0;
    loaded->factory->customUi(alg, ui_btn);

    // Increment length[0] by 1 via encoder turn +1.
    _NT_uiData ui_enc{};
    ui_enc.encoders[0] = 1;
    ui_enc.controls    = 0;
    ui_enc.lastButtons = 0;
    loaded->factory->customUi(alg, ui_enc);

    // length[0] should now be 5 (stored as 4).
    REQUIRE((shiftgate_applet_on_data_request(alg) & 0xFu) == 4u);
}

// ---------------------------------------------------------------------------
// SG12: encoder button press does not crash.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG12: customUi encoder button press does not crash", "[per-applet][shiftgate]") {
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
// SG13: button1 press routes on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftGate SG13: customUi button1 press routes on_aux_button", "[per-applet][shiftgate]") {
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
