// Per-applet test: Burst.
//
// Manifest: shim/include/applet_manifests/Burst.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Burst.h
//
// 10x ticks-per-step note: Burst's Controller() uses Clock(0) and Clock(1)
// to drive accumulator state (ticks_since_clock, bursts_to_go). Bus-level
// fire-count assertions are unreliable. Tests here cover:
//   - default state serialisation (OnDataRequest byte layout)
//   - round-trip serialise/deserialise stability
//   - encoder and button UI routing (no-crash)
//
// OnDataRequest byte layout (vendor Burst.h):
//   bits [0..7]  = number        (default 4)
//   bits [8..15] = spacing       (default 50)
//   bits [16..23]= div + 8       (default div=1, packed as 9)
//   bits [24..31]= jitter        (default 0)
//   bits [32..39]= accel         (default 0, stored as int8_t)
//
// Bus parameter layout (per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Clock"   (default 1)
//   v[1]  = input  bus for "Trigger" (default 2)
//   v[2]  = input  bus for "Number"  (default 3)
//   v[3]  = input  bus for "Spacing" (default 4)
//   v[4]  = output bus for "Burst"   (default 13)
//   v[5]  = output mode for "Burst"  (default 1 = replace)
//   v[6]  = output bus for "Gate"    (default 14)
//   v[7]  = output mode for "Gate"   (default 1 = replace)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Burst.cpp.
uint64_t burst_applet_on_data_request(_NT_algorithm* self);
void     burst_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

static constexpr int kNumFramesBy4 = 8;

// Pack Burst state matching vendor OnDataRequest layout.
// div is stored with a +8 bias (div=1 -> packed=9).
static uint64_t pack_burst(int number, int spacing, int div, int jitter, int accel) {
    uint64_t d = 0;
    d |= (static_cast<uint64_t>(number & 0xFF));
    d |= (static_cast<uint64_t>(spacing & 0xFF)      << 8);
    d |= (static_cast<uint64_t>((div + 8) & 0xFF)    << 16);
    d |= (static_cast<uint64_t>(jitter & 0xFF)        << 24);
    d |= (static_cast<uint64_t>(static_cast<uint8_t>(accel)) << 32);
    return d;
}

TEST_CASE("Burst BU1: OnDataRequest encodes default state after Start", "[burst]") {
    // Vendor Start(): number=4, spacing=50, div=1, jitter=0, accel=0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = burst_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF)               == 4u);    // number
    REQUIRE(((packed >> 8)  & 0xFF)       == 50u);   // spacing
    REQUIRE(((packed >> 16) & 0xFF)       == 9u);    // div=1 + 8 bias
    REQUIRE(((packed >> 24) & 0xFF)       == 0u);    // jitter
    REQUIRE(((packed >> 32) & 0xFF)       == 0u);    // accel
}

TEST_CASE("Burst BU2: serialise round-trip preserves all fields", "[burst]") {
    // Inject number=8, spacing=120, div=2, jitter=10, accel=5 and confirm
    // OnDataRequest returns the same encoding.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_burst(8, 120, 2, 10, 5);
    burst_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = burst_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF)               == 8u);
    REQUIRE(((packed >> 8)  & 0xFF)       == 120u);
    REQUIRE(((packed >> 16) & 0xFF)       == 10u);   // div=2 + 8 = 10
    REQUIRE(((packed >> 24) & 0xFF)       == 10u);
    REQUIRE(((packed >> 32) & 0xFF)       == 5u);
}

TEST_CASE("Burst BU3: encoder turn increments number via customUi", "[burst]") {
    // On_encoder_turn calls OnEncoderMove(1). Default cursor=0 selects number.
    // Default number=4; after one up-turn in edit mode number should be 5.
    // First turn enters edit mode (CursorToggle), second turn increments number.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Verify default number=4.
    REQUIRE((burst_applet_on_data_request(loaded->algorithm) & 0xFF) == 4u);

    // Enter edit mode: press encoder button.
    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Encoder turn +1 in edit mode increments number.
    _NT_uiData ui2{};
    ui2.encoders[0] = 1;
    ui2.controls    = 0;
    ui2.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui2);

    uint64_t packed = burst_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 5u);
}

TEST_CASE("Burst BU4: encoder button press does not crash", "[burst]") {
    // Burst's OnButtonPress() is inherited no-op. Confirm routing completes.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Burst BU5: aux button press does not crash", "[burst]") {
    // on_aux_button routes to OnButtonPress (no-op). Must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Burst BU6: step_impl runs without crash given silent buses", "[burst]") {
    // Run one step with all-zero buses. Verifies the Controller/frame loop
    // does not dereference bad pointers or assert-fail when no clock is present.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float bus[32 * kNumFramesBy4 * 4] = {};
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    REQUIRE(true);
}

TEST_CASE("Burst BU7: positive accel round-trips correctly", "[burst]") {
    // accel is int8_t in the vendor class but Pack/Unpack treat it as unsigned.
    // Positive accel values survive the round-trip intact.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_burst(4, 50, 1, 0, 25);
    burst_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = burst_applet_on_data_request(loaded->algorithm);
    REQUIRE(((packed >> 32) & 0xFF) == 25u);
}
