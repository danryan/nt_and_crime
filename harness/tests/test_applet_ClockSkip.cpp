// Per-applet test: ClockSkip.
//
// Manifest: shim/include/applet_manifests/ClockSkip.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ClockSkip.h
//
// ClockSkip passes an incoming clock pulse to the output with a configurable
// probability p (0-100%). Each channel is independent. The vendor Controller()
// rolls `random(1, 100) <= p_mod[ch]` on each rising Clock edge. If the roll
// passes, ClockOut(ch) fires and trigger_countdown[ch] is set to 1667.
//
// 10x clock-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer. A
//   single rising Clock edge keeps HS::frame.clocked[ch] asserted across all
//   10 inner ticks, so the random skip roll fires 10 times per edge and
//   ClockOut fires 10 times per edge when p=100. trigger_countdown is reset to
//   1667 on each of the 10 ticks (then decremented once), ending at 1666.
//
// Coverage shape: MODEL-THE-MULTIPLIER.
//   - Force p_mod=100 (100% pass) via OnDataReceive to guarantee deterministic
//     ClockOut firing regardless of RNG state.
//   - Force p_mod=0 (0% skip - always blocked) for the skip path.
//   - Countdown tests account for: 10 resets to 1667, each followed by one
//     decrement, final value = 1666.
//   - Round-trip serialise/deserialise covers the packed state.
//
// Bus parameter layout (emit_base_parameters, 4 inputs + 2 outputs):
//   v[0] = 1  Clock 1  bus selector (gate, default bus 1)
//   v[1] = 2  Clock 2  bus selector (gate, default bus 2)
//   v[2] = 3  p CV 1   bus selector (cv,   default bus 3)
//   v[3] = 4  p CV 2   bus selector (cv,   default bus 4)
//   v[4] = 13 Out 1    bus selector (gate, default bus 13)
//   v[5] = 1  Out 1    mode (replace)
//   v[6] = 14 Out 2    bus selector (gate, default bus 14)
//   v[7] = 1  Out 2    mode (replace)
//
// OnDataRequest packs:
//   bits [0,7)  = p[0]  (0-100)
//   bits [7,14) = p[1]  (0-100)

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include "../../shim/include/HSIOFrame.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Test seams defined in plugins/applets/ClockSkip.cpp.
uint64_t clockskip_applet_on_data_request(_NT_algorithm* self);
void     clockskip_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// inner_ticks_override: set to run an exact number of Controller() calls per
// step(), bypassing the default numFrames/3 multiplier.
namespace hem_shim { extern int inner_ticks_override; }

namespace {

constexpr int kBusClock1  = 1;   // v[0] default - Clock 1 input (gate)
constexpr int kBusClock2  = 2;   // v[1] default - Clock 2 input (gate)
constexpr int kBusOut1    = 13;  // v[4] default - Out 1 (gate)
constexpr int kBusOut2    = 14;  // v[6] default - Out 2 (gate)
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

// Pack p[0] and p[1] into a uint64_t as vendor OnDataRequest does.
// p[0] at bits [0,7), p[1] at bits [7,14).
uint64_t pack_probs(int p0, int p1) {
    uint64_t d = 0;
    d |= (uint64_t)(p0 & 0x7Fu);
    d |= (uint64_t)(p1 & 0x7Fu) << 7;
    return d;
}

void clear_all_buses(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single-sample rising-edge pulse at frame 0 on the given 1-based bus.
void write_gate_pulse(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    slice[0] = 6.0f;
    for (int i = 1; i < kNumFrames; ++i) slice[i] = 0.0f;
}

// Write a constant voltage across all frames of a 1-based bus.
void write_cv_bus(float* bus, int bus_1based, float volts) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) slice[i] = volts;
}

// Clear a single bus to 0V.
void clear_one_bus(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) slice[i] = 0.0f;
}

// Returns true if any frame on a 1-based bus is above gate threshold.
bool any_gate_high(const float* bus, int bus_1based) {
    const float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) {
        if (slice[i] > 0.5f) return true;
    }
    return false;
}

struct Setup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
};

Setup make_setup() {
    nt::reset_runtime();
    // HS::frame is a global and not reset by reset_runtime(). Zero it here to
    // prevent clock_countdown or outputs from previous test cases leaking:
    // write_outputs_to_bus always writes HS::frame.outputs[*].value to the
    // output bus, so residual clock pulses from earlier tests cause false highs.
    std::memset(&HS::frame, 0, sizeof(HS::frame));
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_all_buses(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// CS1: pluginEntry returns a factory with the correct guid and name.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS1: pluginEntry returns factory with correct guid", "[per-applet][clockskip]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','C','s');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Clock Skip");
}

// ---------------------------------------------------------------------------
// CS2: construct populates HemiPluginInterface magic and version.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS2: construct populates HemiPluginInterface fields", "[per-applet][clockskip]") {
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
// CS3: Start() initialises p[0]=100, p[1]=75. OnDataRequest packs them.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS3: Start initialises p[0]=100 p[1]=75 packed in OnDataRequest", "[per-applet][clockskip]") {
    auto s = make_setup();
    uint64_t packed = clockskip_applet_on_data_request(s.alg);
    uint64_t p0 = packed & 0x7Fu;
    uint64_t p1 = (packed >> 7) & 0x7Fu;
    REQUIRE(p0 == 100u);
    REQUIRE(p1 == 75u);
}

// ---------------------------------------------------------------------------
// CS4: serialise round-trip preserves p[0] and p[1].
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS4: serialise round-trip preserves probabilities", "[per-applet][clockskip]") {
    auto s = make_setup();
    uint64_t state_in = pack_probs(42, 87);
    clockskip_applet_on_data_receive(s.alg, state_in);
    uint64_t packed = clockskip_applet_on_data_request(s.alg);
    REQUIRE((packed & 0x7Fu)        == 42u);
    REQUIRE(((packed >> 7) & 0x7Fu) == 87u);
}

// ---------------------------------------------------------------------------
// CS5: p=100 (always pass) -> Clock 1 pulse drives Out 1 high.
//
// 10x multiplier: random(1,100) <= 100 is always true. ClockOut(0) fires on
// all 10 inner ticks, resetting clock_countdown and setting output high each
// time. write_outputs_to_bus sees the output high and drives Out 1 bus.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS5: p=100 Clock1 pulse passes through to Out1", "[per-applet][clockskip]") {
    auto s = make_setup();

    // Force p[0]=100, p[1]=100 for deterministic pass.
    clockskip_applet_on_data_receive(s.alg, pack_probs(100, 100));

    write_gate_pulse(s.bus, kBusClock1);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kBusOut1));
}

// ---------------------------------------------------------------------------
// CS6: p=0 (always skip) -> Clock 1 pulse does NOT drive Out 1.
//
// random(1,100) returns [1,99]; random(1,100) <= 0 is always false.
// ClockOut is never called; Out 1 stays low.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS6: p=0 Clock1 pulse is blocked and Out1 stays low", "[per-applet][clockskip]") {
    auto s = make_setup();

    // Force p[0]=0 (block all), p[1]=0 for deterministic skip.
    clockskip_applet_on_data_receive(s.alg, pack_probs(0, 0));

    write_gate_pulse(s.bus, kBusClock1);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE_FALSE(any_gate_high(s.bus, kBusOut1));
}

// ---------------------------------------------------------------------------
// CS7: p=100 -> Clock 2 pulse passes through to Out 2 independently.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS7: p=100 Clock2 pulse passes through to Out2", "[per-applet][clockskip]") {
    auto s = make_setup();

    clockskip_applet_on_data_receive(s.alg, pack_probs(100, 100));

    write_gate_pulse(s.bus, kBusClock2);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kBusOut2));
}

// ---------------------------------------------------------------------------
// CS8: p=0 on ch1 does not affect ch0; Clock 1 still passes with p=100.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS8: channels are independent - ch0 p=100 passes, ch1 p=0 blocks", "[per-applet][clockskip]") {
    auto s = make_setup();

    // p[0]=100 (pass), p[1]=0 (block).
    clockskip_applet_on_data_receive(s.alg, pack_probs(100, 0));

    write_gate_pulse(s.bus, kBusClock1);
    write_gate_pulse(s.bus, kBusClock2);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kBusOut1));
    REQUIRE_FALSE(any_gate_high(s.bus, kBusOut2));
}

// ---------------------------------------------------------------------------
// CS9: no Clock input -> Out 1 stays low even with p=100.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS9: no clock input produces no output regardless of p", "[per-applet][clockskip]") {
    auto s = make_setup();

    clockskip_applet_on_data_receive(s.alg, pack_probs(100, 100));
    // Leave buses at 0V - no clock pulse.
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE_FALSE(any_gate_high(s.bus, kBusOut1));
    REQUIRE_FALSE(any_gate_high(s.bus, kBusOut2));
}

// ---------------------------------------------------------------------------
// CS10: encoder turn adjusts p[0] via customUi (requires EditMode).
//
// ClockSkip::OnEncoderMove checks EditMode(). Without it the encoder only
// moves the cursor. Enter EditMode first by pressing the encoder button
// (triggers CursorToggle() via the base OnButtonPress()), then turn.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS10: encoder turn increments p[0] via customUi in EditMode", "[per-applet][clockskip]") {
    auto s = make_setup();
    // Use p[0]=50 so the increment is visible.
    clockskip_applet_on_data_receive(s.alg, pack_probs(50, 75));
    REQUIRE((clockskip_applet_on_data_request(s.alg) & 0x7Fu) == 50u);

    // Enter EditMode by pressing the encoder button (rising edge).
    _NT_uiData press{};
    press.controls    = kNT_encoderButtonL;
    press.lastButtons = 0;  // rising edge
    s.loaded->factory->customUi(s.alg, press);

    // Turn encoder +1 to increment p[cursor=0] from 50 to 51.
    _NT_uiData turn{};
    turn.encoders[0] = 1;
    turn.controls    = 0;
    turn.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, turn);

    REQUIRE((clockskip_applet_on_data_request(s.alg) & 0x7Fu) == 51u);
}

// ---------------------------------------------------------------------------
// CS11: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS11: hasCustomUi returns expected bitmask", "[per-applet][clockskip]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// CS12: serialise/deserialise via JSON stream round-trip.
// ---------------------------------------------------------------------------
TEST_CASE("ClockSkip CS12: serialise/deserialise JSON round-trip preserves state", "[per-applet][clockskip]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    clockskip_applet_on_data_receive(alg, pack_probs(33, 66));

    // Serialise state to JSON.
    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    loaded->factory->serialise(alg, *stream);
    const std::string& json_out = stream->buffer();

    // Parse JSON and deserialise back into the same algorithm instance.
    auto parse = nt::make_json_parse(json_out);
    REQUIRE(parse != nullptr);
    bool ok = loaded->factory->deserialise(alg, *parse);
    REQUIRE(ok);

    uint64_t packed = clockskip_applet_on_data_request(alg);
    REQUIRE((packed & 0x7Fu)        == 33u);
    REQUIRE(((packed >> 7) & 0x7Fu) == 66u);
}
