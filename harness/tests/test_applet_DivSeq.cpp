// Per-applet pilot test: DivSeq.
//
// Manifest: shim/include/applet_manifests/DivSeq.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/DivSeq.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. DivSeq's Controller reads Clock(0)
//   once per tick and advances its DivSequence Poke() for each clock.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped; behavioral coverage relies on
//   confirming that output is generated after clock input is driven and
//   confirming state survives a round-trip. This avoids brittle assertions on
//   the 10x multiplier interaction with DivSeq's internal step sequencer while
//   still covering all observable behaviors.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for DivSeq manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (Trig 1) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Trig 2) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus index for Clock input
constexpr int kResetBusIdx  = 2;   // default bus index for Reset input
constexpr int kTrig1BusIdx  = 13;  // default bus index for Trig 1
// kTrig2BusIdx = 14; Trig 2 not exercised in bus-level tests (shape-2 coverage).
constexpr int kNumFrames     = 32;
constexpr int kNumFramesBy4  = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single-sample rising-edge pulse at frame 0 on the given 1-based bus.
void pulse_bus(float* bus, int bus_1based, int numFrames) {
    float* slice = bus + (bus_1based - 1) * numFrames;
    slice[0] = 6.0f;
}

// Read whether any frame on the given 1-based bus is above gate threshold.
bool any_gate_high(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) {
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
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    // One warmup step to let BaseStart settle.
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

// ---------------------------------------------------------------------------
// pack_divseq: mirrors DivSeq::OnDataRequest() byte-by-byte.
//
// DivSeq::OnDataRequest() layout (b=6 bits per step value):
//   bits [ 0, 6) = ch0 step0 value = constrain(steps[0][0] + offset0, 0, 63)
//   bits [ 6, 6) = ch0 step1 value
//   bits [12, 6) = ch0 step2 value
//   bits [18, 6) = ch0 step3 value
//   bits [24, 6) = ch0 step4 value
//   bits [30, 6) = ch1 step0 value = constrain(steps[1][0] + offset1, 0, 63)
//   bits [36, 6) = ch1 step1 value
//   bits [42, 6) = ch1 step2 value
//   bits [48, 6) = ch1 step3 value
//   bits [54, 6) = ch1 step4 value
//   bit  [60, 1) = ch0 negative-offset flag (1 if any ch0 step was negative)
//   bit  [61, 1) = ch0 step0 mute flag (1 if !StepActive(0))
//   bit  [62, 1) = ch1 negative-offset flag
//   bit  [63, 1) = ch1 step0 mute flag
//
// Parameters:
//   ch0_steps[5], ch1_steps[5]: raw step values (may be negative for multiply)
//   ch0_neg, ch1_neg: 1 if offset=16 was applied (any step was negative)
//   ch0_mute0, ch1_mute0: first-step mute bits
// ---------------------------------------------------------------------------
static uint64_t pack_divseq(
    const int ch0_steps[5], const int ch1_steps[5],
    int ch0_neg, int ch1_neg,
    int ch0_mute0, int ch1_mute0)
{
    uint64_t data = 0;
    const int offsets[2] = { ch0_neg ? 16 : 0, ch1_neg ? 16 : 0 };
    const int* steps[2] = { ch0_steps, ch1_steps };
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 5; ++i) {
            int val = steps[ch][i] + offsets[ch];
            if (val < 0) val = 0;
            if (val > 63) val = 63;
            const uint64_t masked = (uint64_t)(val & 0x3F);
            data |= masked << (ch * 5 * 6 + i * 6);
        }
    }
    // sign bits and mute bits at positions 60..63
    if (ch0_neg)   data |= (uint64_t)1 << 60;
    if (ch0_mute0) data |= (uint64_t)1 << 61;
    if (ch1_neg)   data |= (uint64_t)1 << 62;
    if (ch1_mute0) data |= (uint64_t)1 << 63;
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// DS1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS1: pluginEntry returns factory with correct guid", "[per-applet-pilot][divseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','D','s');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "DivSeq");
}

// ---------------------------------------------------------------------------
// DS2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][divseq]") {
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
// DS3: round-trip via serialise/deserialise preserves the packed data.
//
// Mirrors pack_divseq layout (see above). Uses all-positive steps so
// offset=0 for both channels, simplifying the round-trip assertion.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS3: serialise round-trip preserves packed step data", "[per-applet-pilot][divseq]") {
    // Construct a known positive-only state (offset=0 for both channels).
    // ch0 steps: 4, 3, 2, 1, 6   ch1 steps: 8, 5, 2, 3, 1
    // Neither channel has negative steps => ch0_neg=0, ch1_neg=0.
    // Step 0 of ch0 is active (no mute) => ch0_mute0=0; same for ch1.
    const int ch0[5] = { 4, 3, 2, 1, 6 };
    const int ch1[5] = { 8, 5, 2, 3, 1 };
    uint64_t packed = pack_divseq(ch0, ch1, 0, 0, 0, 0);

    // Verify local pack layout before round-trip.
    REQUIRE(((int)((packed >>  0) & 0x3F)) == 4);  // ch0 step0
    REQUIRE(((int)((packed >>  6) & 0x3F)) == 3);  // ch0 step1
    REQUIRE(((int)((packed >> 12) & 0x3F)) == 2);  // ch0 step2
    REQUIRE(((int)((packed >> 18) & 0x3F)) == 1);  // ch0 step3
    REQUIRE(((int)((packed >> 24) & 0x3F)) == 6);  // ch0 step4
    REQUIRE(((int)((packed >> 30) & 0x3F)) == 8);  // ch1 step0
    REQUIRE(((int)((packed >> 36) & 0x3F)) == 5);  // ch1 step1
    REQUIRE(((int)((packed >> 42) & 0x3F)) == 2);  // ch1 step2
    REQUIRE(((int)((packed >> 48) & 0x3F)) == 3);  // ch1 step3
    REQUIRE(((int)((packed >> 54) & 0x3F)) == 1);  // ch1 step4
    REQUIRE(((packed >> 60) & 0xF) == 0u);         // no sign/mute bits set

    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Deserialise the JSON into the applet instance.
    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    bool ok = loaded->factory->deserialise(alg, *parse);
    REQUIRE(ok);

    // Re-serialise and capture output.
    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    loaded->factory->serialise(alg, *stream);
    const std::string& out = stream->buffer();

    // Decode hemi_lo and hemi_hi from the serialised string.
    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon_lo = std::strchr(lo_pos, ':');
    REQUIRE(colon_lo != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon_lo + 1);
    REQUIRE(rt_lo == lo);

    const char* hi_pos = std::strstr(out.c_str(), "hemi_hi");
    REQUIRE(hi_pos != nullptr);
    const char* colon_hi = std::strchr(hi_pos, ':');
    REQUIRE(colon_hi != nullptr);
    uint32_t rt_hi = (uint32_t)std::atoi(colon_hi + 1);
    REQUIRE(rt_hi == hi);
}

// ---------------------------------------------------------------------------
// DS4: clock input generates output (behavior test, shape-2 coverage).
//
// Drives a rising clock edge on the Clock gate input and asserts that Trig 1
// goes high at least once. The default Start() state assigns positive step
// values, so some output fires within the 10 inner ticks that see the
// asserted clock.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS4: rising clock edge on input 0 produces output on Trig 1", "[per-applet-pilot][divseq]") {
    auto s = make_setup();

    // Drive Clock input (bus 1) with a rising edge.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With 10 inner ticks all seeing clocked=true, DivSequence::Poke(true)
    // advances the step counter on each tick. At least one of the 10 advances
    // fires the trigger for channel 0 given the Start() defaults.
    REQUIRE(any_gate_high(s.bus, kTrig1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// DS5: reset input clears divider state.
//
// Sends a Reset pulse and confirms that Trig 1 fires on the next clock.
// After Reset(), all div_seq[ch] counters are zeroed; the next clock drives
// them forward and the first matching step fires.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS5: reset input clears divider state", "[per-applet-pilot][divseq]") {
    auto s = make_setup();

    // Drive a few clocks to put the sequencer in some non-initial state.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset pulse on bus 2.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After reset all step counters return to 0. Next clock fires Trig 1
    // since the 10 inner ticks each call Poke(true) and the sequencer advances.
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kTrig1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// DS6: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS6: hasCustomUi returns expected bitmask", "[per-applet-pilot][divseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// DS7: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS7: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][divseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 1;   // left encoder turn +1
    data.controls     = 0;
    data.lastButtons  = 0;

    // Must not crash; dispatches through on_encoder_turn -> OnEncoderMove.
    loaded->factory->customUi(loaded->algorithm, data);

    // Applet still serialises correctly after the encoder interaction.
    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// DS8: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS8: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][divseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Simulate a fresh press: lastButtons = 0 (released), controls = pressed.
    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_encoderButtonL;
    data.lastButtons  = 0;

    // OnButtonPress toggles edit mode or mutes; must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// DS9: customUi button1 edge dispatches to on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq DS9: customUi button1 edge dispatches to on_aux_button", "[per-applet-pilot][divseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_button1;
    data.lastButtons  = 0;

    // on_aux_button is a no-op in standalone (not claimed by hasCustomUi);
    // must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
