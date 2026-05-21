// Per-applet test: Xfader.
//
// Manifest: shim/include/applet_manifests/Xfader.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Xfader.h
//
// 10x-ticks shape: bus-level safe.
// Xfader uses millis() gating (t - last_balance_update > 0) which collapses
// all 10 inner Controller() ticks into a single millisecond-gated update.
// Bus-level output assertions are safe.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Fade L"   (gate, default 1)
//   v[1]  = input  bus for "Fade R"   (gate, default 2)
//   v[2]  = input  bus for "Sig 1"    (cv,   default 3)
//   v[3]  = input  bus for "Sig 2"    (cv,   default 4)
//   v[4]  = output bus for "Mix 1+2"  (cv,   default 13)
//   v[5]  = output mode for "Mix 1+2" (default 1 = replace)
//   v[6]  = output bus for "Mix 2+1"  (cv,   default 14)
//   v[7]  = output mode for "Mix 2+1" (default 1 = replace)
//
// Serialise layout (OnDataRequest):
//   bits  [0, 8)  = balance >> 8   (default 128 = midpoint)
//   bits  [8,24)  = rate           (default 128 = 50%/sec)
//   bits [24,32)  = center         (default 128)
//   bit  [32]     = center_reset_enable (default false)
//
// Mix formula at midpoint balance=128:
//   _balance = 128
//   mix1 = Proportion(128,255,sig2) + Proportion(127,255,sig1)
//   mix2 = Proportion(128,255,sig1) + Proportion(127,255,sig2)
//   With sig1=0, sig2=0: both outputs = 0.
//   With sig1=V, sig2=0: mix2 ≈ (128/255)*V (roughly half signal).
//   At balance=255 (full right): mix1 = sig2, mix2 = sig1.
//   At balance=0   (full left):  mix1 = sig1, mix2 = sig2.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

using Catch::Approx;

// Test seams defined in plugins/applets/Xfader.cpp.
uint64_t xfader_applet_on_data_request(_NT_algorithm* self);
void     xfader_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices matching emit_base_parameters layout.
static constexpr int kBusFadeL  = 1;   // v[0] gate input
static constexpr int kBusFadeR  = 2;   // v[1] gate input
static constexpr int kBusSig1   = 3;   // v[2] cv input
static constexpr int kBusSig2   = 4;   // v[3] cv input
static constexpr int kBusMix1   = 13;  // v[4] cv output "Mix 1+2"
static constexpr int kBusMix2   = 14;  // v[6] cv output "Mix 2+1"

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a gate pulse on the first frame of a 1-based bus (1.0V = high).
static void write_gate_bus(float* busFrames, int bus_1based, bool high) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = high ? 1.0f : 0.0f;
}

static float read_cv_last_frame(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_buses(float* busFrames) {
    for (int bus : {kBusFadeL, kBusFadeR, kBusSig1, kBusSig2, kBusMix1, kBusMix2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Helper: pack the vendor OnDataRequest layout.
// balance_byte = balance >> 8 [0..255], rate [0..65535],
// center [0..255], center_reset_enable [0..1].
static uint64_t pack_xfader(int balance_byte, int rate, int center, int cre) {
    uint64_t d = 0;
    d |= ((uint64_t)(balance_byte & 0xFF));
    d |= ((uint64_t)(rate & 0xFFFF)) << 8;
    d |= ((uint64_t)(center & 0xFF)) << 24;
    d |= ((uint64_t)(cre & 0x1)) << 32;
    return d;
}

TEST_CASE("Xfader XF1: OnDataRequest packs default state after Start", "[per-applet][xfader]") {
    // Start() sets balance=128<<8, rate=128, center=128, center_reset_enable=false.
    // OnDataRequest packs: bits[0,8)=128, bits[8,24)=128, bits[24,32)=128, bit[32]=0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = xfader_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 128u);                     // balance byte
    REQUIRE(((packed >> 8) & 0xFFFF) == 128u);            // rate
    REQUIRE(((packed >> 24) & 0xFF) == 128u);             // center
    REQUIRE(((packed >> 32) & 0x1) == 0u);                // center_reset_enable
}

TEST_CASE("Xfader XF2: serialise round-trip preserves state", "[per-applet][xfader]") {
    // Inject balance=200, rate=1024, center=100, cre=1 and confirm round-trip.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_xfader(200, 1024, 100, 1);
    xfader_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = xfader_applet_on_data_request(loaded->algorithm);

    REQUIRE((packed & 0xFF) == 200u);
    REQUIRE(((packed >> 8) & 0xFFFF) == 1024u);
    REQUIRE(((packed >> 24) & 0xFF) == 100u);
    REQUIRE(((packed >> 32) & 0x1) == 1u);
}

TEST_CASE("Xfader XF3: at midpoint balance with zero inputs, outputs are zero", "[per-applet][xfader]") {
    // Default balance=128. With sig1=0 and sig2=0, mix1 and mix2 must be 0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_cv_last_frame(bus, kBusMix1) == Approx(0.0f).margin(0.01f));
    REQUIRE(read_cv_last_frame(bus, kBusMix2) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Xfader XF4: at balance=0 sig1 appears on mix1 and sig2 on mix2", "[per-applet][xfader]") {
    // With balance=0: _balance=0.
    // mix1 = Proportion(0,255,sig2) + Proportion(255,255,sig1) = sig1.
    // mix2 = Proportion(0,255,sig1) + Proportion(255,255,sig2) = sig2.
    // Inject balance=0 via OnDataReceive then step with sig1=3V, sig2=1V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Force balance=0.
    uint64_t state_in = pack_xfader(0, 128, 0, 0);
    xfader_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusSig1, 3.0f);
    write_cv_bus(bus, kBusSig2, 1.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // At balance=0: mix1 ~ sig1=3V, mix2 ~ sig2=1V.
    REQUIRE(read_cv_last_frame(bus, kBusMix1) == Approx(3.0f).margin(0.1f));
    REQUIRE(read_cv_last_frame(bus, kBusMix2) == Approx(1.0f).margin(0.1f));
}

TEST_CASE("Xfader XF5: at balance=255 sig2 appears on mix1 and sig1 on mix2", "[per-applet][xfader]") {
    // With balance=255: _balance=255.
    // mix1 = Proportion(255,255,sig2) + Proportion(0,255,sig1) = sig2.
    // mix2 = Proportion(255,255,sig1) + Proportion(0,255,sig2) = sig1.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_xfader(255, 128, 128, 0);
    xfader_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusSig1, 3.0f);
    write_cv_bus(bus, kBusSig2, 1.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // At balance=255: mix1 ~ sig2=1V, mix2 ~ sig1=3V.
    REQUIRE(read_cv_last_frame(bus, kBusMix1) == Approx(1.0f).margin(0.1f));
    REQUIRE(read_cv_last_frame(bus, kBusMix2) == Approx(3.0f).margin(0.1f));
}

TEST_CASE("Xfader XF6: encoder turn (no edit mode) adjusts balance and center", "[per-applet][xfader]") {
    // Not in EditMode: OnEncoderMove(1) increases balance by 1<<8=256 and sets center.
    // Starting balance=128<<8=32768, one click -> 32768+256=33024 -> byte=129.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE((xfader_applet_on_data_request(loaded->algorithm) & 0xFF) == 128u);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t packed = xfader_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 129u);
    // center also updated to new balance byte
    REQUIRE(((packed >> 24) & 0xFF) == 129u);
}

TEST_CASE("Xfader XF7: aux button toggles center_reset_enable", "[per-applet][xfader]") {
    // AuxButton() flips center_reset_enable. Default is false (bit 32 = 0).
    // After one press it becomes true (bit 32 = 1).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(((xfader_applet_on_data_request(loaded->algorithm) >> 32) & 1) == 0u);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE(((xfader_applet_on_data_request(loaded->algorithm) >> 32) & 1) == 1u);
}

TEST_CASE("Xfader XF8: encoder button press does not crash", "[per-applet][xfader]") {
    // OnButtonPress is mapped to the encoder button. Xfader's OnButtonPress
    // is a no-op (inherited default). Must not crash.
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
