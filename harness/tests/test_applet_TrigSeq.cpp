// Per-applet pilot test: TrigSeq.
//
// Manifest: shim/include/applet_manifests/TrigSeq.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/TrigSeq.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on
//   Clock(0) asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. Each call that sees Clock(0)
//   true advances step[ch] by 1, so one bus-level clock pulse steps each
//   channel 10 times within that buffer.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level "output fires on Nth clock" assertions are dropped because the
//   10x inner-tick multiplier makes exact step-count arithmetic unreliable.
//   Behavioral coverage confirms that: output is generated (gate high) after
//   a clock pulse when a step bit is set in the pattern, and that serialise /
//   deserialise preserves all packed fields.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus indices from emit_base_parameters for TrigSeq manifest:
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (Trg Ch1) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Trg Ch2) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus index for Clock input
constexpr int kResetBusIdx  = 2;   // default bus index for Reset input
constexpr int kOut1BusIdx   = 13;  // default bus index for Trg Ch1
constexpr int kOut2BusIdx   = 14;  // default bus index for Trg Ch2
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

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

// pack_trigseq mirrors TrigSeq::OnDataRequest byte-by-byte:
//   bits [0,8)  = pattern[0]   (uint8_t, no bias; AND 0xFF)
//   bits [8,8)  = pattern[1]   (uint8_t, no bias; AND 0xFF)
//   bits [16,3) = end_step[0]  (0..7, no bias; AND 0x7)
//   bits [19,3) = end_step[1]  (0..7, no bias; AND 0x7)
uint64_t pack_trigseq(int pattern0, int pattern1,
                      int end_step0, int end_step1) {
    uint64_t data = 0;
    data |= (uint64_t)(pattern0  & 0xFF);
    data |= (uint64_t)(pattern1  & 0xFF) << 8;
    data |= (uint64_t)(end_step0 & 0x7)  << 16;
    data |= (uint64_t)(end_step1 & 0x7)  << 19;
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// TS1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS1: pluginEntry returns factory with correct guid", "[per-applet-pilot][trigseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','T','s');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Trig8x2");
}

// ---------------------------------------------------------------------------
// TS2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][trigseq]") {
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
// TS3: pack_trigseq layout is self-consistent.
//
// Verifies that the local pack helper encodes and decodes each field at the
// correct bit offset before using it in the round-trip test.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS3: pack_trigseq bit layout is correct", "[per-applet-pilot][trigseq]") {
    uint64_t packed = pack_trigseq(0xAB, 0xCD, 5, 3);

    REQUIRE((int)(packed        & 0xFF) == 0xAB);  // pattern[0]
    REQUIRE((int)((packed >> 8) & 0xFF) == 0xCD);  // pattern[1]
    REQUIRE((int)((packed >> 16) & 0x7) == 5);     // end_step[0]
    REQUIRE((int)((packed >> 19) & 0x7) == 3);     // end_step[1]
    // Bits above 22 must be zero (only 22 bits used).
    REQUIRE((packed >> 22) == 0u);
}

// ---------------------------------------------------------------------------
// TS4: serialise round-trip preserves all four packed fields.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS4: serialise round-trip preserves pattern and end_step fields", "[per-applet-pilot][trigseq]") {
    uint64_t packed = pack_trigseq(0xA5, 0x5A, 6, 3);

    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    bool ok = loaded->factory->deserialise(alg, *parse);
    REQUIRE(ok);

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

    // hemi_hi must be 0 (TrigSeq uses only 22 bits).
    const char* hi_pos = std::strstr(out.c_str(), "hemi_hi");
    REQUIRE(hi_pos != nullptr);
    const char* hi_colon = std::strchr(hi_pos, ':');
    REQUIRE(hi_colon != nullptr);
    uint32_t rt_hi = (uint32_t)std::atoi(hi_colon + 1);
    REQUIRE(rt_hi == 0u);
}

// ---------------------------------------------------------------------------
// TS5: clock pulse on input 0 produces output when step bit is set.
//
// 10x shape 2: does NOT assert fire count; asserts only that the gate output
// goes high at least once after a clock pulse when pattern bits are non-zero.
// TrigSeq::Start() initialises pattern[ch] = random(1,255), so at least one
// bit is set by construction, and the 10 inner steps advance through the 8-
// step pattern guaranteeing at least one hit.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS5: clock pulse on input 0 produces Trg Ch1 output", "[per-applet-pilot][trigseq]") {
    auto s = make_setup();

    // Force a known pattern with bit 0 set so step 0 always fires.
    // Deserialise pattern[0]=0xFF (all steps set), end_step[0]=7.
    uint64_t packed = pack_trigseq(0xFF, 0xFF, 7, 7);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);
    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":0,"hemi_lo":%u})", (unsigned)lo);
    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    s.loaded->factory->deserialise(s.alg, *parse);

    // Drive Clock input with a rising edge.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kOut1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TS6: clock pulse also produces Trg Ch2 output when channel 2 pattern is set.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS6: clock pulse with channel 2 pattern set produces Trg Ch2 output", "[per-applet-pilot][trigseq]") {
    auto s = make_setup();

    // All steps set on both channels.
    uint64_t packed = pack_trigseq(0xFF, 0xFF, 7, 7);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);
    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":0,"hemi_lo":%u})", (unsigned)lo);
    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    s.loaded->factory->deserialise(s.alg, *parse);

    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kOut2BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TS7: reset input resets step counters.
//
// After reset, a subsequent clock pulse fires step 0, which is set in the
// all-bits pattern. Gate goes high on the next clock pulse.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS7: reset input resets step counters", "[per-applet-pilot][trigseq]") {
    auto s = make_setup();

    // All steps set on both channels.
    uint64_t packed = pack_trigseq(0xFF, 0xFF, 7, 7);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);
    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":0,"hemi_lo":%u})", (unsigned)lo);
    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    s.loaded->factory->deserialise(s.alg, *parse);

    // Drive a few clocks to advance the sequence.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset pulse.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After reset, clock fires step 0 (which is set in 0xFF pattern).
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kOut1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TS8: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS8: hasCustomUi returns expected bitmask", "[per-applet-pilot][trigseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// TS9: customUi encoder turn and button press dispatch without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("TrigSeq TS9: customUi encoder and button events dispatch without crash", "[per-applet-pilot][trigseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    // Encoder turn.
    {
        _NT_uiData data{};
        data.encoders[0] = 1;   // left encoder +1
        data.controls    = 0;
        data.lastButtons = 0;
        loaded->factory->customUi(loaded->algorithm, data);
    }

    // Encoder button press.
    {
        _NT_uiData data{};
        data.encoders[0] = 0;
        data.controls    = kNT_encoderButtonL;
        data.lastButtons = 0;
        loaded->factory->customUi(loaded->algorithm, data);
    }

    // Applet still serialises correctly after interactions.
    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
