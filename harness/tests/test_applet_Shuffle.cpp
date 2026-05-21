// Per-applet test: Shuffle.
//
// Manifest: shim/include/applet_manifests/Shuffle.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Shuffle.h
//
// Shuffle schedules a delayed clock output via `next_trigger = tick + delay_ticks`
// inside if(Clock(0)). Because `tick` is sampled once per Controller() call and
// the inner 10x loop reuses it, the scheduled tick lands in the past for the
// next 9 inner ticks, causing immediate misfires on every inner tick.
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer.
//   Tests MUST:
//     - Use opaque accessors for state injection.
//     - Use hem_shim::inner_ticks_override = 1 between clock edges for
//       deterministic single-step coverage.
//     - NEVER assert bus-level fire counts on shuffled outputs.
//
// Coverage shape: SHAPE 2 (state-injection + round-trip only; no bus-level
//   fire-count assertions on shuffled or triplet outputs).
//
// Bus parameter layout (emit_base_parameters):
//   v[0]  = 1   Clock     bus (gate, default bus 1)
//   v[1]  = 2   Reset     bus (gate, default bus 2)
//   v[2]  = 3   Odd Mod   bus (cv,   default bus 3)
//   v[3]  = 4   Even      bus (cv,   default bus 4)
//   v[4]  = 13  Shuffle   output bus (gate, default bus 13)
//   v[5]  = 1   Shuffle   output mode (replace)
//   v[6]  = 14  Triplets  output bus (gate, default bus 14)
//   v[7]  = 1   Triplets  output mode (replace)
//
// OnDataRequest packs:
//   bits [0, 7) = delay[0] (even-clock delay, 0..99, no bias)
//   bits [7,14) = delay[1] (odd-clock delay,  0..99, no bias)

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include <cstring>

// Test seams defined in plugins/applets/Shuffle.cpp.
uint64_t shuffle_applet_on_data_request(_NT_algorithm* self);
void     shuffle_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// inner_ticks_override: set to 1 to run exactly one Controller() tick per
// step() call, eliminating the 10x multiplier during single-edge tests.
namespace hem_shim { extern int inner_ticks_override; }

namespace {

constexpr int kBusClock    = 1;   // v[0] default - Clock input (gate)
constexpr int kBusReset    = 2;   // v[1] default - Reset input (gate)
constexpr int kBusShuffle  = 13;  // v[4] default - Shuffle output (gate)
constexpr int kBusTriplets = 14;  // v[6] default - Triplets output (gate)
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_all_buses(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single rising-edge gate pulse at frame 0 on a 1-based bus.
void write_gate_pulse(float* bus, int bus_1based) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    dst[0] = 6.0f;
    for (int i = 1; i < kNumFrames; ++i) dst[i] = 0.0f;
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

// Encode delay[0] and delay[1] into a packed data word (14 bits total).
uint64_t encode_delays(int delay0, int delay1) {
    return ((uint64_t)(delay0 & 0x7F)) | ((uint64_t)(delay1 & 0x7F) << 7);
}

}  // namespace

TEST_CASE("Shuffle SH1: Start() defaults both delays to 0", "[per-applet][shuffle]") {
    // Vendor Start() sets delay[0]=0, delay[1]=0. OnDataRequest packs both
    // into 14 bits: bits [0,7) = delay[0], bits [7,14) = delay[1].
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = shuffle_applet_on_data_request(loaded->algorithm);
    int d0 = (int)(packed        & 0x7F);
    int d1 = (int)((packed >> 7) & 0x7F);
    REQUIRE(d0 == 0);
    REQUIRE(d1 == 0);
}

TEST_CASE("Shuffle SH2: round-trip serialization preserves both delay values", "[per-applet][shuffle]") {
    // OnDataReceive + OnDataRequest must preserve arbitrary delay[0] and
    // delay[1] values across a save/load cycle.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(37, 82));
    uint64_t packed = shuffle_applet_on_data_request(loaded->algorithm);
    REQUIRE((int)(packed        & 0x7F) == 37);
    REQUIRE((int)((packed >> 7) & 0x7F) == 82);
}

TEST_CASE("Shuffle SH3: round-trip preserves boundary values (0 and 99)", "[per-applet][shuffle]") {
    // Confirm that the maximum valid delay value (99) survives the pack cycle
    // without truncation (7 bits can hold 0..127, so 99 fits cleanly).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(0, 99));
    uint64_t packed = shuffle_applet_on_data_request(loaded->algorithm);
    REQUIRE((int)(packed        & 0x7F) ==  0);
    REQUIRE((int)((packed >> 7) & 0x7F) == 99);

    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(99, 0));
    packed = shuffle_applet_on_data_request(loaded->algorithm);
    REQUIRE((int)(packed        & 0x7F) == 99);
    REQUIRE((int)((packed >> 7) & 0x7F) ==  0);
}

TEST_CASE("Shuffle SH4: step() runs without crash after reset edge", "[per-applet][shuffle]") {
    // Send a Reset pulse (Digital 2) and confirm step() does not crash.
    // Uses inner_ticks_override=1 to avoid 10x multiplier interaction.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);

    hem_shim::inner_ticks_override = 1;
    write_gate_pulse(bus, kBusReset);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // State must round-trip cleanly after the reset.
    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(50, 50));
    uint64_t packed = shuffle_applet_on_data_request(loaded->algorithm);
    REQUIRE((int)(packed        & 0x7F) == 50);
    REQUIRE((int)((packed >> 7) & 0x7F) == 50);
}

TEST_CASE("Shuffle SH5: at delay=0 shuffle output fires on clock edge (inner_ticks_override=1)", "[per-applet][shuffle]") {
    // With delay[0]=delay[1]=0, Proportion(0, 100, tempo)=0, so
    // next_trigger=tick. The first clock seeds last_tick only (tempo=0).
    // The second clock with inner_ticks_override=1 fires ClockOut(0) in the
    // same Controller() call because tick==next_trigger.
    //
    // Uses inner_ticks_override=1 for both steps to ensure a single-tick
    // Controller() call (avoiding 10x-multiplier misfires).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(0, 0));

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);

    // Step 1: seed last_tick (first clock edge, tempo not yet set).
    hem_shim::inner_ticks_override = 1;
    write_gate_pulse(bus, kBusClock);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_one_bus(bus, kBusClock);
    clear_one_bus(bus, kBusShuffle);

    // Step 2: second clock edge. With tempo set and delay=0,
    // next_trigger=tick so ClockOut(0) should fire.
    hem_shim::inner_ticks_override = 1;
    write_gate_pulse(bus, kBusClock);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // We do not assert a specific fire count but confirm output is present.
    REQUIRE(any_gate_high(bus, kBusShuffle) == true);
}

TEST_CASE("Shuffle SH6: triplet output fires on first clock edge", "[per-applet][shuffle]") {
    // On the first Clock(0) edge with triplet_which==0, next_trip_trigger=tick
    // (downbeat). ClockOut(1) fires immediately in that same Controller() tick.
    // Uses inner_ticks_override=1 to keep the Controller() at a single tick.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(0, 0));

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);

    hem_shim::inner_ticks_override = 1;
    write_gate_pulse(bus, kBusClock);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Triplet downbeat fires on the first clock edge.
    REQUIRE(any_gate_high(bus, kBusTriplets) == true);
}

TEST_CASE("Shuffle SH7: state round-trips cleanly after multiple step() calls with no clock", "[per-applet][shuffle]") {
    // Confirms that running step() repeatedly without any clock input does not
    // corrupt applet state. State round-trip is the stable observable here.
    // Bus-level fire-count assertions are omitted per SHAPE 2 coverage rules
    // (ClockOut holds output high for 175 ticks after a spurious tick=0 match).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject known delay values before running.
    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(42, 17));

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);

    // Run 10 steps with no gate input.
    for (int i = 0; i < 10; ++i) {
        hem_shim::inner_ticks_override = 1;
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    // Delay values must survive unmodified - Controller() does not touch them.
    uint64_t packed = shuffle_applet_on_data_request(loaded->algorithm);
    REQUIRE((int)(packed        & 0x7F) == 42);
    REQUIRE((int)((packed >> 7) & 0x7F) == 17);
}

TEST_CASE("Shuffle SH8: encoder turn changes delay value via customUi", "[per-applet][shuffle]") {
    // Drive _NT_uiData with encoders[0]=1. on_encoder_turn routes to
    // OnEncoderMove(1) which increments delay[cursor]. Default cursor=1 so
    // delay[1] increments from 0 to 1.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Confirm initial state: both delays == 0.
    uint64_t packed = shuffle_applet_on_data_request(loaded->algorithm);
    REQUIRE((int)(packed & 0x7F) == 0);
    REQUIRE((int)((packed >> 7) & 0x7F) == 0);

    // Put applet in edit mode so encoder moves adjust delay (not cursor).
    _NT_uiData ui_press{};
    ui_press.controls    = kNT_encoderButtonL;
    ui_press.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_press);

    _NT_uiData ui{};
    ui.encoders[0]  = 1;
    ui.controls     = 0;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    packed = shuffle_applet_on_data_request(loaded->algorithm);
    // delay[1] (cursor=1 in edit mode) should be 1 after one increment.
    REQUIRE((int)((packed >> 7) & 0x7F) == 1);
}

TEST_CASE("Shuffle SH9: serialise/deserialise round-trip via JSON stream", "[per-applet][shuffle]") {
    // Confirm that the factory serialise + deserialise hooks preserve state.
    // We inject a known state, serialise it to JSON, then build a fresh parse
    // from the JSON text and deserialise it back, then compare OnDataRequest.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(55, 33));

    // Serialise current state to a JSON stream.
    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    loaded->factory->serialise(loaded->algorithm, *stream);
    const std::string& json = stream->buffer();

    // Reset state to defaults, then deserialise from the captured JSON.
    shuffle_applet_on_data_receive(loaded->algorithm, encode_delays(0, 0));
    auto parse = nt::make_json_parse(json);
    REQUIRE(parse != nullptr);
    bool ok = loaded->factory->deserialise(loaded->algorithm, *parse);
    REQUIRE(ok);

    uint64_t after = shuffle_applet_on_data_request(loaded->algorithm);
    REQUIRE((int)(after        & 0x7F) == 55);
    REQUIRE((int)((after >> 7) & 0x7F) == 33);
}
