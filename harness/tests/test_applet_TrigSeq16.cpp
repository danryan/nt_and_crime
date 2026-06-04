// Per-applet pilot test: TrigSeq16.
//
// Manifest: shim/include/applet_manifests/TrigSeq16.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/TrigSeq16.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. TrigSeq16's Controller advances the
//   step counter inside `if (Clock(0))`, so one bus-level rising edge advances
//   the sequencer 10 steps, not 1.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped; behavioral coverage relies on
//   confirming that output is generated after clock input is driven (using a
//   known all-ones pattern so all steps fire) and confirming state survives a
//   round-trip. This avoids brittle math on the 10x multiplier while still
//   covering all observable behaviors.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for TrigSeq16 manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (Trg) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Inverse) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx   = 1;   // default bus index for Clock input
constexpr int kResetBusIdx   = 2;   // default bus index for Reset input
constexpr int kTrgBusIdx     = 13;  // default bus index for Trg output
constexpr int kInvBusIdx     = 14;  // default bus index for Inverse output
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

// pack_trigseq16 mirrors TrigSeq16::OnDataRequest byte-by-byte.
//   bits  [0, 8) = pattern[0]  (uint8, no bias)
//   bits  [8, 8) = pattern[1]  (uint8, no bias)
//   bits [16, 4) = end_step    (4-bit, no bias, range 0..15)
static uint64_t pack_trigseq16(int pattern0, int pattern1, int end_step) {
    uint64_t data = 0;
    data |= (uint64_t)(pattern0 & 0xFF);
    data |= (uint64_t)(pattern1 & 0xFF) << 8;
    data |= (uint64_t)(end_step & 0x0F) << 16;
    return data;
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

}  // namespace

// ---------------------------------------------------------------------------
// TS16_1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_1: pluginEntry returns factory with correct guid", "[per-applet-pilot][trigseq16]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','T','6');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Trig16");
}

// ---------------------------------------------------------------------------
// TS16_2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][trigseq16]") {
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
// TS16_3: round-trip via serialise/deserialise preserves all three packed fields.
//
// Mirrors the pack_trigseq16 layout from OnDataRequest:
//   bits  [0, 8) = pattern[0]  (uint8, no bias)
//   bits  [8, 8) = pattern[1]  (uint8, no bias)
//   bits [16, 4) = end_step    (4-bit, range 0..15)
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_3: serialise round-trip preserves pattern and end_step fields", "[per-applet-pilot][trigseq16]") {
    // Verify local pack bit layout before round-trip.
    uint64_t packed = pack_trigseq16(0xA5, 0x5A, 11);
    REQUIRE((int)(packed        & 0xFF)        == 0xA5);  // pattern[0]
    REQUIRE((int)((packed >>  8) & 0xFF)       == 0x5A);  // pattern[1]
    REQUIRE((int)((packed >> 16) & 0x0F)       == 11);    // end_step
    REQUIRE((packed >> 20) == 0u);  // TrigSeq16 uses only 20 bits; hi is 0

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

    // Decode hemi_lo from the serialised string.
    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);
    REQUIRE(rt_lo == lo);
}

// ---------------------------------------------------------------------------
// TS16_4: clock input generates output on Trg when pattern has all bits set.
//
// Injects an all-ones pattern (0xFF for both channels) via deserialise so that
// every step fires. Drives a clock edge and asserts Trg goes high. Does NOT
// assert fire-count because the 10x clocked multiplier makes exact counting
// unreliable without modelling the inner-tick budget explicitly.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_4: rising clock edge produces Trg output with all-ones pattern", "[per-applet-pilot][trigseq16]") {
    auto s = make_setup();

    // Inject all-ones pattern: every step active, full 16-step length.
    uint64_t packed = pack_trigseq16(0xFF, 0xFF, 15);
    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    bool ok = s.loaded->factory->deserialise(s.alg, *parse);
    REQUIRE(ok);

    // Run warmup again after deserialization to clear any stale state.
    clear_bus(s.bus);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    clear_bus(s.bus);

    // Drive Clock input (bus 1) with a rising edge pulse.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With all-ones pattern, every step fires on Trg. Confirm gate-high.
    REQUIRE(any_gate_high(s.bus, kTrgBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TS16_5: all-zeros pattern produces Inverse output.
//
// Injects a zero pattern (0x00 for both channels) via deserialise so that
// no step has its bit set. Every clock step fires Inverse (the complement
// channel). Per SHAPE 2 coverage, we assert the POSITIVE case only: Inverse
// IS high. We do not assert absence of Trg because HS::frame.clock_countdown
// persists across test cases (HEMISPHERE_CLOCK_TICKS = 175, draining ~18
// buffers), making absence assertions unreliable in multi-test runs.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_5: all-zeros pattern produces Inverse output", "[per-applet-pilot][trigseq16]") {
    auto s = make_setup();

    // Inject all-zeros pattern.
    uint64_t packed = pack_trigseq16(0x00, 0x00, 15);
    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    bool ok = s.loaded->factory->deserialise(s.alg, *parse);
    REQUIRE(ok);

    // Run warmup again after deserialization.
    clear_bus(s.bus);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    clear_bus(s.bus);

    // Drive Clock input (bus 1) with a rising edge pulse.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // No bits set: Inverse SHOULD fire (complement output active).
    REQUIRE(any_gate_high(s.bus, kInvBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TS16_6: Reset input resets step position.
//
// Advances several steps, then sends Reset. After reset the sequence restarts
// from step 0 on the next clock. With an all-ones pattern both Trg channels
// fire regardless of step position, so this test confirms Reset does not
// crash and serialise still produces valid output.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_6: Reset input does not crash and sequence continues cleanly", "[per-applet-pilot][trigseq16]") {
    auto s = make_setup();

    // Inject all-ones pattern.
    uint64_t packed = pack_trigseq16(0xFF, 0xFF, 15);
    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    s.loaded->factory->deserialise(s.alg, *parse);

    // Advance a few steps.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset pulse on bus 2.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clock after reset: should still produce output with all-ones pattern.
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kTrgBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TS16_7: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_7: hasCustomUi returns expected bitmask", "[per-applet-pilot][trigseq16]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// TS16_8: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_8: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][trigseq16]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 1;   // left encoder turn +1
    data.controls     = 0;
    data.lastButtons  = 0;

    // Must not crash; dispatches through on_encoder_turn.
    loaded->factory->customUi(loaded->algorithm, data);

    // Applet still serialises correctly after the encoder interaction.
    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// TS16_9: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq16 TS16_9: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][trigseq16]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Simulate a fresh press: lastButtons = 0 (released), controls = pressed.
    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_encoderButtonL;
    data.lastButtons  = 0;

    // OnButtonPress cycles cursor; must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
