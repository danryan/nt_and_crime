// Per-applet pilot test: SequenceX.
//
// Manifest: shim/include/applet_manifests/SequenceX.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/SequenceX.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. SequenceX reads Clock(0) once per
//   tick so one bus-level rising edge advances the step up to 10 times.
//
//   Coverage shape: SHAPE 2 (round-trip + behavioral output check only; no
//   fire-count assertions). Bus-level assertions confirm output appears after
//   a clock edge; exact step-count arithmetic is not modelled.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus layout from emit_base_parameters<SequenceX>:
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (CV) bus selector,   default 13
//   v[3] = output 0 mode,                default 1 (replace)
//   v[4] = output 1 (Step 1) bus selector, default 14
//   v[5] = output 1 mode,                default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus for Clock input
constexpr int kResetBusIdx  = 2;   // default bus for Reset input
constexpr int kCVBusIdx     = 13;  // default bus for CV output
constexpr int kStep1BusIdx  = 14;  // default bus for Step 1 gate output
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

// Return true if any frame on the given 1-based bus exceeds gate threshold.
bool any_gate_high(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) {
        if (slice[i] > 0.5f) return true;
    }
    return false;
}

// Return true if any CV frame is non-zero on the given 1-based bus.
bool any_cv_nonzero(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) {
        if (slice[i] != 0.0f) return true;
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
// pack_sequencex: mirrors SequenceX::OnDataRequest byte-by-byte.
//
// Layout (SEQX_STEPS=8, b=6 bits per step):
//   bits [s*6, 6)    = note[s] - SEQX_MIN_VALUE  (bias +30, range 0..60)
//   bits [48,  8)    = muted (8-bit mask)
//
// note[s] in [-30, 30], stored as (note[s] + 30) & 0x3F
// muted is an 8-bit field at bit 48 (crosses lo/hi boundary at bit 32).
// ---------------------------------------------------------------------------

static uint64_t pack_sequencex(const int note[8], int muted) {
    constexpr int b = 6;
    constexpr int SEQX_MIN_VALUE = -30;
    uint64_t data = 0;
    for (int s = 0; s < 8; ++s) {
        uint64_t field = (uint64_t)((note[s] - SEQX_MIN_VALUE) & 0x3F);
        data |= field << (s * b);
    }
    data |= (uint64_t)(muted & 0xFF) << (8 * b);
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// SX1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX1: pluginEntry returns factory with correct guid", "[per-applet-pilot][sequencex]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','S','x');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Seq8");
}

// ---------------------------------------------------------------------------
// SX2: construct populates HemiPluginInterface magic and version.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][sequencex]") {
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
// SX3: serialise round-trip preserves all packed fields.
//
// The pack layout spans 56 bits total:
//   notes [0..7] at 6 bits each (bits 0..47),
//   muted 8 bits at bit 48 (bits 48..55).
// hemi_lo = bits [0,32), hemi_hi = bits [32,64).
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX3: serialise round-trip preserves note and muted fields", "[per-applet-pilot][sequencex]") {
    // Build a known pack and verify the helper layout before the round-trip.
    const int notes[8] = { 5, -10, 0, 15, -5, 20, -20, 10 };
    const int muted_val = 0xA5;  // alternating bits, non-trivial pattern
    uint64_t packed = pack_sequencex(notes, muted_val);

    // Spot-check a few fields.
    constexpr int SEQX_MIN_VALUE = -30;
    REQUIRE(((int)((packed >> 0)  & 0x3F) + SEQX_MIN_VALUE) == notes[0]);  // note[0]
    REQUIRE(((int)((packed >> 6)  & 0x3F) + SEQX_MIN_VALUE) == notes[1]);  // note[1]
    REQUIRE(((int)((packed >> 42) & 0x3F) + SEQX_MIN_VALUE) == notes[7]);  // note[7]
    REQUIRE((int)((packed >> 48) & 0xFF) == muted_val);                     // muted

    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(packed >> 32);

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

    // Decode hemi_lo and hemi_hi from the serialised output.
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
// SX4: clock input generates CV output (behavior, shape-2 coverage).
//
// Start() calls Randomize(true) which fills note[s] with values in [0, 30].
// After a clock edge the sequencer outputs CV continuously. With 10 inner
// Controller ticks per buffer all seeing Clock(0)=true, the step advances
// (wrapping modulo 8) and CV is written every tick from the current step's
// note. Confirm CV bus is non-zero after a clock edge.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX4: rising clock edge on input 0 produces non-zero CV output", "[per-applet-pilot][sequencex]") {
    auto s = make_setup();

    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // SequenceX computes play_note every tick regardless of clock, so CV is
    // always written; confirm it appears on the CV output bus.
    REQUIRE(any_cv_nonzero(s.bus, kCVBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// SX5: reset input returns Step 1 gate high on next clock.
//
// After a reset, step = 0 and on the next Clock the applet fires ClockOut(1)
// because step == 0. The 10x multiplier means the 10 inner ticks each see
// the clock; on the first tick (step==0 after reset) ClockOut(1) fires.
// Confirm Step 1 gate bus goes high after reset + one clock.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX5: reset followed by clock produces Step 1 gate", "[per-applet-pilot][sequencex]") {
    auto s = make_setup();

    // Advance a few clocks to move away from step 0.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Next clock: step == 0 after reset, so ClockOut(1) fires.
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kStep1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// SX6: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX6: hasCustomUi returns expected bitmask", "[per-applet-pilot][sequencex]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// SX7: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX7: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][sequencex]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 1;  // left encoder turn +1
    data.controls     = 0;
    data.lastButtons  = 0;

    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SX8: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX8: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][sequencex]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_encoderButtonL;
    data.lastButtons  = 0;

    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SX9: customUi button1 edge dispatches to on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("SequenceX SX9: customUi button1 edge dispatches to on_aux_button", "[per-applet-pilot][sequencex]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_button1;
    data.lastButtons  = 0;

    // on_aux_button is a documented no-op in standalone; must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
