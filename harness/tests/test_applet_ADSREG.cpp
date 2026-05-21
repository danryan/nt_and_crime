// Per-applet test: ADSREG.
//
// Manifest: shim/include/applet_manifests/ADSREG.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ADSREG.h
//
// 10x ticks-per-step concern: Controller() runs for each of the 10 inner
// ticks. The envelope MiniADSR::Process() advances stage_ticks each tick, so
// amplitude changes are multiplied. Gate tests here use round-trip state
// injection and amplitude-presence checks rather than counting exact ticks.
// The 10x multiplier means a single step() call with a gate asserted will
// already progress into the attack stage, producing non-zero amplitude.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Gate 1"   (default 1)  -- gate ch0
//   v[1]  = input  bus for "Gate 2"   (default 2)  -- gate ch1
//   v[2]  = input  bus for "Release1" (default 3)  -- cv mod ch0 release
//   v[3]  = input  bus for "Release2" (default 4)  -- cv mod ch1 release
//   v[4]  = output bus for "Amp 1"    (default 13)
//   v[5]  = output mode for "Amp 1"   (default 1 = replace)
//   v[6]  = output bus for "Amp 2"    (default 14)
//   v[7]  = output mode for "Amp 2"   (default 1 = replace)
//
// Vendor OnDataRequest packs (for both channels ch=0,1):
//   bits [ch*32 +  0, 8)  = setting[ATTACK_STAGE]
//   bits [ch*32 +  8, 8)  = setting[DECAY_STAGE]
//   bits [ch*32 + 16, 8)  = setting[SUSTAIN_STAGE]
//   bits [ch*32 + 24, 8)  = setting[RELEASE_STAGE]
//
// Default values after Start():
//   ch0: A=10, D=30, S=120, R=25
//   ch1: A=20, D=30, S=120, R=35

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/ADSREG.cpp.
uint64_t adsreg_on_data_request(_NT_algorithm* self);
void     adsreg_on_data_receive(_NT_algorithm* self, uint64_t data);
// Resets the global enc_edit[LEFT_HEMISPHERE] edit mode flag so navigation
// tests start from a known-off state regardless of prior test side effects.
void     adsreg_reset_edit_mode();

// Bus indices matching default parameter layout.
static constexpr int kBusGate1 = 1;   // v[0] default
static constexpr int kBusGate2 = 2;   // v[1] default
static constexpr int kBusRel1  = 3;   // v[2] default
static constexpr int kBusRel2  = 4;   // v[3] default
static constexpr int kBusAmp1  = 13;  // v[4] default
static constexpr int kBusAmp2  = 14;  // v[6] default

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant value across all frames of a 1-based bus.
// volts > 0.5 is sufficient to trigger a gate in the shim.
static void write_gate_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static float read_last_frame(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_buses(float* busFrames) {
    for (int bus : {kBusGate1, kBusGate2, kBusRel1, kBusRel2, kBusAmp1, kBusAmp2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Pack helper mirrors vendor OnDataRequest for one channel starting at bit_offset.
static uint64_t pack_adsreg_channel(int attack, int decay, int sustain, int release, int bit_offset) {
    uint64_t data = 0;
    data |= (static_cast<uint64_t>(attack  & 0xFF) << (bit_offset +  0));
    data |= (static_cast<uint64_t>(decay   & 0xFF) << (bit_offset +  8));
    data |= (static_cast<uint64_t>(sustain & 0xFF) << (bit_offset + 16));
    data |= (static_cast<uint64_t>(release & 0xFF) << (bit_offset + 24));
    return data;
}

TEST_CASE("ADSREG AR1: OnDataRequest packs default settings after Start", "[per-applet][adsreg]") {
    // Vendor Start() sets ch0: A=10,D=30,S=120,R=25; ch1: A=20,D=30,S=120,R=35.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = adsreg_on_data_request(loaded->algorithm);

    // Channel 0 (bits 0..31)
    REQUIRE(((packed >>  0) & 0xFF) == 10u);   // A
    REQUIRE(((packed >>  8) & 0xFF) == 30u);   // D
    REQUIRE(((packed >> 16) & 0xFF) == 120u);  // S
    REQUIRE(((packed >> 24) & 0xFF) == 25u);   // R

    // Channel 1 (bits 32..63)
    REQUIRE(((packed >> 32) & 0xFF) == 20u);   // A
    REQUIRE(((packed >> 40) & 0xFF) == 30u);   // D
    REQUIRE(((packed >> 48) & 0xFF) == 120u);  // S
    REQUIRE(((packed >> 56) & 0xFF) == 35u);   // R
}

TEST_CASE("ADSREG AR2: serialise round-trip preserves custom settings", "[per-applet][adsreg]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject custom values for both channels.
    uint64_t state_in = pack_adsreg_channel(50, 80, 200, 100, 0)
                      | pack_adsreg_channel(60, 90, 180, 110, 32);
    adsreg_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = adsreg_on_data_request(loaded->algorithm);

    REQUIRE(((packed >>  0) & 0xFF) == 50u);
    REQUIRE(((packed >>  8) & 0xFF) == 80u);
    REQUIRE(((packed >> 16) & 0xFF) == 200u);
    REQUIRE(((packed >> 24) & 0xFF) == 100u);
    REQUIRE(((packed >> 32) & 0xFF) == 60u);
    REQUIRE(((packed >> 40) & 0xFF) == 90u);
    REQUIRE(((packed >> 48) & 0xFF) == 180u);
    REQUIRE(((packed >> 56) & 0xFF) == 110u);
}

TEST_CASE("ADSREG AR3: gate high on ch0 produces non-zero amplitude on Amp1", "[per-applet][adsreg]") {
    // With gate asserted and 10 inner Controller ticks per step(), the attack
    // stage runs 10 ticks advancing the amplitude. Amp1 output should be > 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate1, 5.0f);  // gate ch0 high

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_last_frame(bus, kBusAmp1) > 0.0f);
}

TEST_CASE("ADSREG AR4: gate low on ch0 produces zero amplitude on Amp1", "[per-applet][adsreg]") {
    // Without a gate, the envelope stays in NO_STAGE and amplitude is 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    // No gate; Amp1 bus starts at 0.

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_last_frame(bus, kBusAmp1) == 0.0f);
}

TEST_CASE("ADSREG AR5: gate high on ch1 produces non-zero amplitude on Amp2", "[per-applet][adsreg]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate2, 5.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_last_frame(bus, kBusAmp2) > 0.0f);
}

TEST_CASE("ADSREG AR6: encoder turn advances attack setting via customUi", "[per-applet][adsreg]") {
    // Default cursor=0 -> ATTACK_STAGE on ch0. Encoder +1 increments attack from 10 to 11.
    // Must first enter edit mode (button press toggles EditMode).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Ensure edit mode starts OFF so the button press below reliably turns it ON.
    adsreg_reset_edit_mode();

    // Press button to toggle edit mode on.
    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Turn encoder +1 to increment attack.
    ui = {};
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t packed = adsreg_on_data_request(loaded->algorithm);
    REQUIRE(((packed >> 0) & 0xFF) == 11u);  // attack was 10, now 11
}

TEST_CASE("ADSREG AR7: encoder turn in navigation mode moves cursor", "[per-applet][adsreg]") {
    // Without edit mode, encoder moves cursor. After cursor advances to 1
    // (DECAY on ch0) and we enter edit mode, encoder +1 increments decay
    // (default 30 -> 31), not attack.
    //
    // enc_edit[] is a process-global that persists across test cases.
    // adsreg_reset_edit_mode() sets it to OFF before this test so the
    // navigation encoder turn is guaranteed not to be in edit mode.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Force edit mode OFF regardless of prior test state.
    adsreg_reset_edit_mode();

    // Navigate cursor to position 1 (DECAY stage on ch0).
    _NT_uiData ui{};
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Enter edit mode.
    ui = {};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Increment decay.
    ui = {};
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t packed = adsreg_on_data_request(loaded->algorithm);
    REQUIRE(((packed >>  0) & 0xFF) == 10u);  // attack unchanged
    REQUIRE(((packed >>  8) & 0xFF) == 31u);  // decay incremented from 30
}

TEST_CASE("ADSREG AR8: encoder button press toggles edit mode without crash", "[per-applet][adsreg]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("ADSREG AR9: aux button routes on_button_press without crash", "[per-applet][adsreg]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("ADSREG AR10: draw does not crash", "[per-applet][adsreg]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    bool result = loaded->factory->draw(loaded->algorithm);
    REQUIRE(result == true);
}
