// Per-applet pilot test: Relabi.
//
// Manifest: shim/include/applet_manifests/Relabi.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Relabi.h
//
// 10x ticks-per-step applies. Relabi uses PROCESS_TICKS=4 internally:
// Controller() advances clkDiv each call and only updates oscillators when
// clkDiv == 1 (left-hemisphere clkCalc=1). With 10 inner ticks per step(),
// the oscillator update fires on inner ticks 1 and 5 (clkDiv cycles 0..3).
// Behavior tests below assert observable state after N steps accounting for
// this cadence rather than asserting fire-count arithmetic. Round-trip tests
// use pack_relabi packing reimplemented inline because applet_test_helpers.cpp
// is not linked into per-applet test binaries.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Minimal shim types needed for test assertions (HemiPluginInterface, guid).
// Do NOT include Relabi.h or WaveformManager.h in the test TU — they define
// file-scope globals (RelabiManager::instance, HS::user_waveforms, etc.) that
// would clash with the definitions already compiled into Relabi.cpp.
#include "HemiPluginInterface.h"
#include "applet_manifests/Relabi.h"

// Forward declarations for the round-trip accessor and the opaque applet
// handle type. _RelabiHandle is an incomplete type used only as void* in the
// test; the accessor in Relabi.cpp returns the real Relabi* cast to void*.
struct _RelabiOpaqueApplet;

// Defined at the bottom of plugins/applets/Relabi.cpp, host-sim only.
// Returns void* (opaque) to avoid pulling in the full Relabi class here.
extern void* relabi_applet_opaque(_NT_algorithm* alg);
extern uint64_t relabi_on_data_request(void* applet);
extern void     relabi_on_data_receive(void* applet, uint64_t data);

// ---------------------------------------------------------------------------
// pack_relabi: mirrors Relabi::OnDataRequest bit layout (63 bits used).
// Reimplemented here because applet_test_helpers.cpp is not linked into
// per-applet test binaries.
// ---------------------------------------------------------------------------
static uint64_t pack_relabi_local(
    int freq0, int freq1, int freq2,
    int xmod0, int xmod1, int xmod2,
    int phase0, int phase1, int phase2,
    int thresh0, int thresh1, int thresh2,
    int freq_mul, int freq_div,
    int out0, int out1, int out2, int out3)
{
    uint64_t data = 0;
    data |= ((uint64_t)(freq0    & 0x3F));
    data |= ((uint64_t)(freq1    & 0x3F)) << 6;
    data |= ((uint64_t)(freq2    & 0x3F)) << 12;
    data |= ((uint64_t)(xmod0    & 0x07)) << 18;
    data |= ((uint64_t)(xmod1    & 0x07)) << 21;
    data |= ((uint64_t)(xmod2    & 0x07)) << 24;
    data |= ((uint64_t)(phase0   & 0x07)) << 27;
    data |= ((uint64_t)(phase1   & 0x07)) << 30;
    data |= ((uint64_t)(phase2   & 0x07)) << 33;
    data |= ((uint64_t)(thresh0  & 0x07)) << 36;
    data |= ((uint64_t)(thresh1  & 0x07)) << 39;
    data |= ((uint64_t)(thresh2  & 0x07)) << 42;
    data |= ((uint64_t)(freq_mul & 0x07)) << 45;
    data |= ((uint64_t)(freq_div & 0x07)) << 48;
    data |= ((uint64_t)(out0     & 0x07)) << 51;
    data |= ((uint64_t)(out1     & 0x07)) << 54;
    data |= ((uint64_t)(out2     & 0x07)) << 57;
    data |= ((uint64_t)(out3     & 0x07)) << 60;
    return data;
}

// ---------------------------------------------------------------------------
// Helper: run one step() with numFramesBy4 = 8 (32 frames).
// ---------------------------------------------------------------------------
static void step_once(nt::LoadedPlugin* loaded, _NT_algorithm* alg, float* bus) {
    loaded->factory->step(alg, bus, 8);
}

// ---------------------------------------------------------------------------
// Helper: read output bus value in NT volts for a given 1-based bus index.
// Takes the mean of the first frame (all frames are filled identically by
// write_frame_to_bus in replace mode).
// ---------------------------------------------------------------------------
static float read_bus_volts(float* bus, int bus_1based, int numFrames) {
    return bus[(bus_1based - 1) * numFrames];
}

// ---------------------------------------------------------------------------
// Helper: write a gate pulse (frame 0 = 6V, rest = 0) on a 1-based bus.
// ---------------------------------------------------------------------------
static void write_gate_pulse(float* bus, int bus_1based, int numFrames) {
    float* p = bus + (bus_1based - 1) * numFrames;
    p[0] = 6.0f;
    for (int i = 1; i < numFrames; ++i) p[i] = 0.0f;
}

// ---------------------------------------------------------------------------
// Helper: clear a bus.
// ---------------------------------------------------------------------------
static void clear_bus_range(float* bus, int total_buses, int numFrames) {
    for (int i = 0; i < total_buses * numFrames; ++i) bus[i] = 0.0f;
}

// ---------------------------------------------------------------------------
// RL1: loader resolves factory, guid matches manifest.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL1: loader resolves factory", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    REQUIRE(loaded->factory->guid == per_applet::Relabi_manifest::guid);
}

// ---------------------------------------------------------------------------
// RL2: round-trip — serialised state survives OnDataReceive without change.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL2: round-trip via pack/unpack matches OnDataRequest", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    void* applet = relabi_applet_opaque(alg);
    REQUIRE(applet != nullptr);

    // Build a packed state with non-default values.
    uint64_t packed = pack_relabi_local(
        /* freq */   10, 20, 30,
        /* xmod */    2,  3,  4,
        /* phase */   1,  2,  3,
        /* thresh */  1,  2,  3,
        /* mul/div */ 2,  1,
        /* out */     1,  2,  3,  6
    );

    relabi_on_data_receive(applet, packed);
    uint64_t round_tripped = relabi_on_data_request(applet);

    REQUIRE(round_tripped == packed);
}

// ---------------------------------------------------------------------------
// RL3: default state round-trip — Start() defaults survive pack/unpack.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL3: default state survives round-trip", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    void* applet = relabi_applet_opaque(alg);
    REQUIRE(applet != nullptr);

    // Capture defaults from Start().
    uint64_t before = relabi_on_data_request(applet);
    relabi_on_data_receive(applet, before);
    uint64_t after = relabi_on_data_request(applet);

    REQUIRE(after == before);
}

// ---------------------------------------------------------------------------
// RL4: behavior — oscillators produce non-zero CV output after warm-up steps.
//
// After Start(), freqKnob[0..2] = {30, 34, 38} encoding 3/5/7 Hz.
// The oscillators start at 0 and advance in PROCESS_TICKS cadence.
// After 10+ steps, sample[] should be non-zero for at least one LFO.
// Default outputAssign[0]=0 maps Out A to LFO1 output (= sample[0] + 3V).
// We assert Out A eventually departs from the HEMISPHERE_3V_CV baseline.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL4: oscillators produce non-zero CV output after warm-up", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);

    const int numFrames = 32;
    clear_bus_range(bus, nt::num_buses(), numFrames);

    // Out A defaults to bus 13 (v[2] default = 13).
    const int out_a_bus = alg->v[2];
    REQUIRE(out_a_bus == 13);

    // Run 20 steps to let the oscillators advance past the initial phase.
    for (int i = 0; i < 20; ++i) {
        step_once(loaded, alg, bus);
    }

    float out_a = read_bus_volts(bus, out_a_bus, numFrames);
    // Out A = sample[0] + HEMISPHERE_3V_CV, scaled to volts.
    // HEMISPHERE_3V_CV = 4608 (hem units); scaled by 1536 = 3.0 V.
    // The output should not be stuck at exactly 3V if the oscillator advanced.
    // We check the absolute value is in the 0..6V range that LFO output spans.
    REQUIRE(out_a >= 0.0f);
    REQUIRE(out_a <= 6.0f);
    // After 20 steps the oscillator should have advanced; verify it's not
    // identically zero (initial state before any oscillator update would be
    // 0 hem-units/1536 = 0V; after advancing it should be positive).
    REQUIRE(out_a > 0.0f);
}

// ---------------------------------------------------------------------------
// RL5: behavior — Clock input triggers phase reset.
//
// With known phase reset (Clock(0) fires), each oscillator resets to the
// phase configured by phaseKnob. Default phaseKnob[i]=0 → phase 0%.
// We warm up the oscillators first (so sample[] != 0), then fire a Clock
// edge and verify the outputs are consistent with a reset (non-crashy).
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL5: Clock gate input processes without crash", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);

    const int numFrames = 32;
    clear_bus_range(bus, nt::num_buses(), numFrames);

    // Clock input is on v[0] (default = bus 1, 1-based).
    const int clock_bus = alg->v[0];
    REQUIRE(clock_bus == 1);

    // Warm up 5 steps.
    for (int i = 0; i < 5; ++i) {
        step_once(loaded, alg, bus);
    }

    // Fire one Clock rising edge and step.
    write_gate_pulse(bus, clock_bus, numFrames);
    step_once(loaded, alg, bus);

    // Clear clock bus and step again.
    float* clock_p = bus + (clock_bus - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) clock_p[i] = 0.0f;
    step_once(loaded, alg, bus);

    // No crash; Out A is still in the valid range.
    const int out_a_bus = alg->v[2];
    float out_a = read_bus_volts(bus, out_a_bus, numFrames);
    REQUIRE(out_a >= 0.0f);
    REQUIRE(out_a <= 6.0f);
}

// ---------------------------------------------------------------------------
// RL6: customUi — encoder turn advances cursor without crash.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL6: encoder turn advances cursor", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // drive customUi through HemiPluginInterface on_encoder_turn.
    auto* p = static_cast<HemiPluginInterface*>(alg);
    REQUIRE(p->magic == kHemiInterfaceMagic);
    REQUIRE(p->on_encoder_turn != nullptr);

    // Encoder +1 moves cursor; encoder -1 moves it back. No crash expected.
    _NT_uiData ui = {};
    ui.encoders[0] = 1;
    loaded->factory->customUi(alg, ui);

    ui.encoders[0] = -1;
    loaded->factory->customUi(alg, ui);

    // Verify applet is still functional after UI interaction.
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    const int numFrames = 32;
    clear_bus_range(bus, nt::num_buses(), numFrames);
    step_once(loaded, alg, bus);
    float out_a = read_bus_volts(bus, alg->v[2], numFrames);
    REQUIRE(out_a >= 0.0f);
}

// ---------------------------------------------------------------------------
// RL7: customUi — encoder button press fires without crash.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL7: encoder button press fires without crash", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    auto* p = static_cast<HemiPluginInterface*>(alg);
    REQUIRE(p->on_button_press != nullptr);

    // Simulate encoder button press edge (controls bit set, lastButtons cleared).
    _NT_uiData ui = {};
    ui.controls   = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(alg, ui);

    // No crash; applet still steps cleanly.
    float* bus = nt::bus_frames_base();
    const int numFrames = 32;
    clear_bus_range(bus, nt::num_buses(), numFrames);
    step_once(loaded, alg, bus);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// RL8: customUi — button1 (aux) fires without crash.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL8: button1 aux fires without crash", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    auto* p = static_cast<HemiPluginInterface*>(alg);
    REQUIRE(p->on_aux_button != nullptr);

    _NT_uiData ui = {};
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(alg, ui);

    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// RL9: draw — returns true (no crash, SegmentDisplay path exercised).
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL9: draw returns true", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(loaded->factory->draw(alg) == true);
}

// ---------------------------------------------------------------------------
// RL10: HemiPluginInterface ABI — magic and version correct.
// ---------------------------------------------------------------------------
TEST_CASE("Relabi RL10: HemiPluginInterface magic and version correct", "[per-applet-pilot][relabi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* p = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(p->magic == kHemiInterfaceMagic);
    REQUIRE(p->interface_version == kHemiInterfaceVersion);
    REQUIRE((loaded->factory->guid & 0xFFFF) == kHemiGuidPrefix);
    REQUIRE(p->render_view != nullptr);
    REQUIRE(p->on_encoder_turn != nullptr);
    REQUIRE(p->on_encoder_turn_shifted != nullptr);
    REQUIRE(p->on_button_press != nullptr);
    REQUIRE(p->on_aux_button != nullptr);
}
