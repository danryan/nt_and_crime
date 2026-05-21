// Per-applet test: Shredder.
//
// Manifest: shim/include/applet_manifests/Shredder.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Shredder.h
//
// Shredder is a Cartesian sequencer: a 4x4 grid of random voltages stepped
// by a clock input. Reset returns to step 0. X/Y CV inputs jump the play
// position directly. Both outputs are quantized by default (quant_channels=0).
//
// 10x multiplier note: Clock(0) and Clock(1) are gate-driven accumulators.
// A single rising edge on Clock fires 10 Controller() calls in one step()
// buffer. `step` advances (or Reset() runs) 10 times per edge. Tests that
// care about step position use state-injection via OnDataReceive to set
// known seed/range, then assert output presence rather than specific step
// counts. Tests that only care about output range or round-trip use
// bus-level-safe shapes.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Clock"  (gate, default 1)
//   v[1]  = input  bus for "Reset"  (gate, default 2)
//   v[2]  = input  bus for "X pos"  (cv,   default 3)
//   v[3]  = input  bus for "Y pos"  (cv,   default 4)
//   v[4]  = output bus for "Ch 1"   (cv,   default 13)
//   v[5]  = output mode for "Ch 1"  (default 1 = replace)
//   v[6]  = output bus for "Ch 2"   (cv,   default 14)
//   v[7]  = output mode for "Ch 2"  (default 1 = replace)
//
// OnDataRequest layout:
//   bits [ 0, 4): range[0]
//   bit  [4]:     bipolar[0]
//   bit  [5]:     shred_on_reset[0]
//   bits [ 8,12): range[1]
//   bit  [12]:    bipolar[1]
//   bit  [13]:    shred_on_reset[1]
//   bits [16,24): quant_channels
//   bits [24,32): GetScale(0)
//   bits [32,48): seed[0]
//   bits [48,64): seed[1]

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Shredder.cpp.
uint64_t shredder_applet_on_data_request(_NT_algorithm* self);
void     shredder_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices for Shredder's parameter layout.
static constexpr int kBusClock = 1;   // v[0]
static constexpr int kBusReset = 2;   // v[1]
static constexpr int kBusXPos  = 3;   // v[2]
static constexpr int kBusYPos  = 4;   // v[3]
static constexpr int kBusOut1  = 13;  // v[4]
static constexpr int kBusOut2  = 14;  // v[6]

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

static void write_gate_bus(float* busFrames, int bus_1based, bool high) {
    float v = high ? 5.0f : 0.0f;
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = v;
}

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static float read_last_frame(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_buses(float* busFrames) {
    for (int bus : {kBusClock, kBusReset, kBusXPos, kBusYPos, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Pack a state word matching OnDataRequest layout.
// range0 in bits [0,4), bipolar0 in bit 4, sor0 in bit 5,
// range1 in bits [8,12), bipolar1 in bit 12, sor1 in bit 13,
// quant_ch in [16,24), scale in [24,32), seed0 in [32,48), seed1 in [48,64).
static uint64_t pack_state(int range0, bool bipolar0, bool sor0,
                            int range1, bool bipolar1, bool sor1,
                            int quant_ch, int scale,
                            uint16_t seed0, uint16_t seed1) {
    uint64_t d = 0;
    d |= (uint64_t)(range0 & 0xF);
    d |= (uint64_t)(bipolar0 ? 1 : 0) << 4;
    d |= (uint64_t)(sor0 ? 1 : 0) << 5;
    d |= (uint64_t)(range1 & 0xF) << 8;
    d |= (uint64_t)(bipolar1 ? 1 : 0) << 12;
    d |= (uint64_t)(sor1 ? 1 : 0) << 13;
    d |= (uint64_t)(quant_ch & 0xFF) << 16;
    d |= (uint64_t)(scale & 0xFF) << 24;
    d |= (uint64_t)seed0 << 32;
    d |= (uint64_t)seed1 << 48;
    return d;
}

TEST_CASE("Shredder SH1: OnDataRequest packs default Start state", "[per-applet][shredder]") {
    // Start() sets range={1,0}, bipolar={false,false}, shred_on_reset={false,false},
    // quant_channels=0, scale=0, seed={0,0} (seeds randomised in Shred; initial
    // seeds are not deterministic, so only the non-seed fields are checked).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = shredder_applet_on_data_request(loaded->algorithm);

    // range[0]=1 in bits [0,4)
    REQUIRE((packed & 0xFu) == 1u);
    // bipolar[0]=false: bit 4 = 0
    REQUIRE(((packed >> 4) & 0x1u) == 0u);
    // shred_on_reset[0]=false: bit 5 = 0
    REQUIRE(((packed >> 5) & 0x1u) == 0u);
    // range[1]=0 in bits [8,12)
    REQUIRE(((packed >> 8) & 0xFu) == 0u);
    // bipolar[1]=false: bit 12 = 0
    REQUIRE(((packed >> 12) & 0x1u) == 0u);
    // quant_channels=0 in bits [16,24)
    REQUIRE(((packed >> 16) & 0xFFu) == 0u);
}

TEST_CASE("Shredder SH2: serialise round-trip preserves range and bipolar fields", "[per-applet][shredder]") {
    // Inject known state and confirm non-seed fields survive round-trip.
    // range0=3, bipolar0=true, range1=2, bipolar1=false, quant_ch=1, scale=5.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_state(3, true, false, 2, false, false, 1, 5, 0xABCD, 0x1234);
    shredder_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = shredder_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFu)          == 3u);   // range[0]
    REQUIRE(((packed >> 4) & 0x1u)   == 1u);   // bipolar[0]
    REQUIRE(((packed >> 5) & 0x1u)   == 0u);   // shred_on_reset[0]
    REQUIRE(((packed >> 8) & 0xFu)   == 2u);   // range[1]
    REQUIRE(((packed >> 12) & 0x1u)  == 0u);   // bipolar[1]
    REQUIRE(((packed >> 16) & 0xFFu) == 1u);   // quant_channels
    REQUIRE(((packed >> 24) & 0xFFu) == 5u);   // scale
    REQUIRE(((packed >> 32) & 0xFFFFu) == 0xABCDu);  // seed[0]
    REQUIRE(((packed >> 48) & 0xFFFFu) == 0x1234u);  // seed[1]
}

TEST_CASE("Shredder SH3: range=0 produces zero output on Ch 1", "[per-applet][shredder]") {
    // When range[0]=0, Shred() sets all sequence[0][i]=0, so Ch 1 always outputs 0.
    // Inject state with range[0]=0 and run a clock step.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_state(0, false, false, 0, false, false, 0, 0, 0, 0);
    shredder_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusClock, true);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_last_frame(bus, kBusOut1);
    // With range=0 all steps are 0, so output must be 0V.
    REQUIRE(out1 == Catch::Approx(0.0f).margin(0.05f));
}

TEST_CASE("Shredder SH4: range=0 on Ch 2 always produces zero output", "[per-applet][shredder]") {
    // Default Start() has range[1]=0; Ch 2 must always output 0V regardless of clock.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusClock, true);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out2 = read_last_frame(bus, kBusOut2);
    REQUIRE(out2 == Catch::Approx(0.0f).margin(0.05f));
}

TEST_CASE("Shredder SH5: non-zero range on Ch 1 produces non-zero output after clock", "[per-applet][shredder]") {
    // Inject seed=12345 (non-zero) with range[0]=3 (unipolar). The randomised
    // sequence will have at least one non-zero entry. After several steps the
    // output must be non-zero at some point; we run 4 steps and check max.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject deterministic state: range0=3, seed0=12345 (random but fixed).
    uint64_t state_in = pack_state(3, false, false, 0, false, false, 0, 0, 12345, 0);
    shredder_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    float max_out = 0.0f;
    for (int edge = 0; edge < 4; ++edge) {
        clear_buses(bus);
        write_gate_bus(bus, kBusClock, true);
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
        float v = read_last_frame(bus, kBusOut1);
        if (v > max_out) max_out = v;

        // Let clock go low between edges.
        clear_buses(bus);
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }
    // With seed 12345 and range 3 (~36V range), at least one step is non-zero.
    REQUIRE(max_out > 0.0f);
}

TEST_CASE("Shredder SH6: encoder turn advances cursor via customUi", "[per-applet][shredder]") {
    // Encoder turn +1 with cursor at 0 (CHAN1_RANGE in edit mode) increments range[0].
    // We start with range[0]=1 (default Start). First put cursor into edit mode via
    // button press, then turn encoder.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Confirm default range[0]=1.
    REQUIRE((shredder_applet_on_data_request(loaded->algorithm) & 0xFu) == 1u);

    // Toggle edit mode for cursor 0 (CHAN1_RANGE).
    _NT_uiData ui_btn{};
    ui_btn.encoders[0] = 0;
    ui_btn.controls    = kNT_encoderButtonL;
    ui_btn.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_btn);

    // Turn encoder +1: range[0] should go 1 -> 2.
    _NT_uiData ui_enc{};
    ui_enc.encoders[0] = 1;
    ui_enc.controls    = 0;
    ui_enc.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_enc);

    REQUIRE((shredder_applet_on_data_request(loaded->algorithm) & 0xFu) == 2u);
}

TEST_CASE("Shredder SH7: aux button routes to AuxButton without crash", "[per-applet][shredder]") {
    // AuxButton() calls Shred(cursor, true) when cursor < 2. Must not crash.
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

TEST_CASE("Shredder SH8: encoder button press routes on_button_press via customUi", "[per-applet][shredder]") {
    // OnButtonPress with cursor=0 (CHAN1_RANGE, not edit mode) records click_tick.
    // Must not crash.
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
