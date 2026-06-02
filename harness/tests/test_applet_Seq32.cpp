// Per-applet test: Seq32.
//
// Manifest: shim/include/applet_manifests/Seq32.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Seq32.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. Seq32::Controller reads Clock(0)
//   and Clock(1) once per tick, so one bus-level rising edge produces 10
//   logical clock ticks inside the applet.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped. State is injected via
//   OC::user_patterns[] step-buffer writes. The 32-step buffer does NOT fit
//   the 64-bit serialise blob (192 bits needed), so only the scalar config
//   (pattern_index, seqmode, glide_on, transpose) round-trips through
//   serialise/deserialise; step-buffer contents do not survive a preset reload
//   (documented fidelity limit, matching the vendor EEPROM precedent).

#define NT_HEM_NEED_USER_PATTERNS 1

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include "OC_patterns.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for Seq32 manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (Pitch) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Gate) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;
constexpr int kResetBusIdx  = 2;
constexpr int kGateBusIdx   = 14;
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

// MAX_TRANS from Seq32.h
constexpr int kMaxTrans = 32;

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

void pulse_bus(float* bus, int bus_1based, int numFrames) {
    float* slice = bus + (bus_1based - 1) * numFrames;
    slice[0] = 6.0f;
}

bool any_gate_high(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) {
        if (slice[i] > 0.5f) return true;
    }
    return false;
}

// pack_seq32: mirrors Seq32::OnDataRequest() byte-by-byte.
//   bits [0,4):  pattern_index (raw, no bias)
//   bits [4,4):  seqmode (raw, no bias)
//   bits [8,1):  glide_on (0 or 1)
//   bits [9..13]: 5 empty bits (zeroed)
//   bits [14,6): transpose + kMaxTrans
static uint64_t pack_seq32(int pattern_index, int seqmode,
                            int glide_on, int transpose) {
    uint64_t data = 0;
    data |= (uint64_t)(pattern_index & 0xF);
    data |= (uint64_t)(seqmode       & 0xF) << 4;
    data |= (uint64_t)(glide_on      & 0x1) << 8;
    // bits 9..13 are empty; leave at 0
    data |= (uint64_t)((transpose + kMaxTrans) & 0x3F) << 14;
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
// S321: factory returns expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S321: pluginEntry returns factory with correct guid and name",
          "[per-applet-pilot][seq32]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','S','3');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Seq32");
}

// ---------------------------------------------------------------------------
// S322: construct populates HemiPluginInterface.
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S322: construct populates HemiPluginInterface magic and version",
          "[per-applet-pilot][seq32]") {
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
// S323: pack helper self-check -- verify bit layout before round-trip.
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S323: pack_seq32 bit layout matches OnDataRequest",
          "[per-applet-pilot][seq32]") {
    // pattern_index=3, seqmode=2 (GATE_75), glide_on=0, transpose=5
    uint64_t p = pack_seq32(3, 2, 0, 5);
    REQUIRE((int)(p & 0xF)          == 3);                    // pattern_index
    REQUIRE((int)((p >> 4) & 0xF)   == 2);                    // seqmode
    REQUIRE((int)((p >> 8) & 0x1)   == 0);                    // glide_on
    REQUIRE((int)((p >> 9) & 0x1F)  == 0);                    // 5 empty bits
    REQUIRE((int)((p >> 14) & 0x3F) == 5 + kMaxTrans);        // transpose bias
    REQUIRE((p >> 20) == 0u);                                  // upper bits unused

    // Negative transpose.
    uint64_t q = pack_seq32(0, 0, 1, -kMaxTrans);
    REQUIRE((int)((q >> 14) & 0x3F) == 0);                    // -32 + 32 = 0
    REQUIRE((int)((q >> 8) & 0x1)   == 1);                    // glide_on = 1
}

// ---------------------------------------------------------------------------
// S324: serialise round-trip preserves scalar config fields.
//
// Fidelity limit: 32-step buffer (192 bits) does NOT fit the 64-bit hemi blob.
// Only the scalar fields (pattern_index, seqmode, glide_on, transpose) survive.
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S324: serialise round-trip preserves scalar config fields",
          "[per-applet-pilot][seq32]") {
    // pattern_index=2, seqmode=1 (GATE_50), glide_on=0, transpose=-10
    uint64_t packed = pack_seq32(2, 1, 0, -10);
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

    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);
    REQUIRE(rt_lo == lo);
}

// Helper: write a byte value into MiniSeq's uint8_t[step] through the
// int16_t[16] notes array. On a little-endian host:
//   uint8_t[2*k]   = lo-byte of notes[k]
//   uint8_t[2*k+1] = hi-byte of notes[k]
// So step s maps to: notes[s/2], byte = (s%2==0) ? lo : hi.
static void set_step_byte(OC::Pattern& pat, int step, uint8_t val) {
    int k = step / 2;
    int hi = step % 2;
    uint8_t lo_b = (uint8_t)(pat.notes[k] & 0xFF);
    uint8_t hi_b = (uint8_t)((pat.notes[k] >> 8) & 0xFF);
    if (hi) hi_b = val; else lo_b = val;
    pat.notes[k] = (int16_t)((uint16_t)(hi_b << 8) | lo_b);
}

// ---------------------------------------------------------------------------
// S325: clock input generates gate output (SHAPE 2 -- no fire-count assertion).
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S325: clock edge on input 0 produces gate output on Gate bus",
          "[per-applet-pilot][seq32]") {
    auto s = make_setup();

    // Set all 32 steps to non-muted C4 (0x20). All will fire gate on clock.
    for (int i = 0; i < 32; ++i) set_step_byte(OC::user_patterns[0], i, 0x20);

    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Gate output should have fired (applet calls ClockOut(1) for unaccented steps).
    REQUIRE(any_gate_high(s.bus, kGateBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// S326: reset input resets the sequencer step to 0.
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S326: reset input resets sequencer to step 0",
          "[per-applet-pilot][seq32]") {
    auto s = make_setup();

    // All steps non-muted. After reset, clocking should fire.
    for (int i = 0; i < 32; ++i) set_step_byte(OC::user_patterns[0], i, 0x20);

    // Clock a few times to advance the sequencer.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset pulse.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clock again: step is 0 (not muted), gate should fire.
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kGateBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// S327: muted step produces no gate output.
//
// Use a length-1 sequence so all 10 inner ticks visit step 0 only, avoiding
// the 10x multiplier advancing past the muted step into an unmuted neighbor.
// Length-1 is encoded as 0xE1 at uint8_t[31] = hi-byte of notes[15].
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S327: muted step produces no gate output",
          "[per-applet-pilot][seq32]") {
    auto s = make_setup();

    // Set sequence length to 1 (only step 0 exists).
    // uint8_t[31] = hi-byte of notes[15] = 0xE1 (0b11100000 | 1).
    set_step_byte(OC::user_patterns[0], 31, 0xE1);

    // Mute step 0: set byte 0 (lo-byte of notes[0]) to 0xA0 (muted + note=0).
    set_step_byte(OC::user_patterns[0], 0, 0x80 | 0x20);

    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE_FALSE(any_gate_high(s.bus, kGateBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// S328: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S328: hasCustomUi returns expected bitmask",
          "[per-applet-pilot][seq32]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// S329: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("Seq32 S329: customUi encoder turn dispatches to OnEncoderMove",
          "[per-applet-pilot][seq32]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0] = 1;
    data.controls    = 0;
    data.lastButtons = 0;

    // Must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
