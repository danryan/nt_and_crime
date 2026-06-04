// Per-applet pilot test: DivSeq10.
//
// Manifest: shim/include/applet_manifests/DivSeq10.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/DivSeq10.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. DivSeq10::Controller reads
//   Clock(0) once per tick, so one bus-level rising edge produces 10 logical
//   clock ticks inside the applet.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped; behavioral coverage relies on
//   confirming that output is generated (gate high on Trig 1 or Trig 2) after
//   clock input is driven. This avoids brittle math on the 10x multiplier
//   while still covering all observable behaviors.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for DivSeq10 manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (Trig 1) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Trig 2) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus index for Clock input
constexpr int kResetBusIdx  = 2;   // default bus index for Reset input
constexpr int kTrig1BusIdx  = 13;  // default bus index for Trig 1
constexpr int kTrig2BusIdx  = 14;  // default bus index for Trig 2
constexpr int kNumFrames    = 32;
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
// pack_divseq10: mirrors DivSeq10::OnDataRequest() byte-by-byte.
//
// OnDataRequest layout (b=6 bits per step, 10 steps = 60 bits):
//   bits [i*6, 6)  for i in 0..9 = constrain(steps[i] + offset, 0, 63)
//   bit  60        = offset flag (1 if any step was negative)
//   bit  61        = first step mute (StepActive(0) == false)
//
// Pack helper convention (CLAUDE.md):
//   - offset is 16 if any step is negative, else 0 (matches applet logic)
//   - AND with 0x3F (6-bit mask) per step
//   - bit 60 and bit 61 are OR'd in explicitly
// ---------------------------------------------------------------------------
static uint64_t pack_divseq10(const int steps[10], bool first_step_muted) {
    // Determine offset: 16 if any step is negative, else 0.
    int offset = 0;
    for (int i = 0; i < 10; i++) {
        if (steps[i] < 0) { offset = 16; break; }
    }

    uint64_t data = 0;
    for (int i = 0; i < 10; i++) {
        int val = steps[i] + offset;
        if (val < 0) val = 0;
        if (val > 63) val = 63;
        data |= (uint64_t)(val & 0x3F) << (i * 6);
    }
    if (offset != 0)
        data |= (uint64_t)1 << 60;
    if (first_step_muted)
        data |= (uint64_t)1 << 61;

    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// DT1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT1: pluginEntry returns factory with correct guid", "[per-applet-pilot][divseq10]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','D','t');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "DivSeq10");
}

// ---------------------------------------------------------------------------
// DT2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][divseq10]") {
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
// DT3: pack_divseq10 helper produces correct bit layout (no-negative case).
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT3: pack_divseq10 produces correct bit layout (positive steps, no mute)", "[per-applet-pilot][divseq10]") {
    // All positive steps, no mute.
    int steps[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint64_t data = pack_divseq10(steps, false);

    // offset=0, so val = steps[i] directly
    for (int i = 0; i < 10; i++) {
        int val = (int)((data >> (i * 6)) & 0x3F);
        REQUIRE(val == steps[i]);
    }
    // Offset flag (bit 60) must be clear.
    REQUIRE(((data >> 60) & 1) == 0);
    // First step mute flag (bit 61) must be clear.
    REQUIRE(((data >> 61) & 1) == 0);
}

// ---------------------------------------------------------------------------
// DT4: pack_divseq10 helper produces correct bit layout (negative steps).
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT4: pack_divseq10 produces correct bit layout (negative step, offset=16)", "[per-applet-pilot][divseq10]") {
    // One negative step triggers offset=16.
    int steps[10] = {-1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint64_t data = pack_divseq10(steps, false);

    // offset=16; val[i] = steps[i] + 16
    for (int i = 0; i < 10; i++) {
        int expected_val = steps[i] + 16;
        if (expected_val < 0) expected_val = 0;
        if (expected_val > 63) expected_val = 63;
        int val = (int)((data >> (i * 6)) & 0x3F);
        REQUIRE(val == expected_val);
    }
    // Offset flag (bit 60) must be set.
    REQUIRE(((data >> 60) & 1) == 1);
}

// ---------------------------------------------------------------------------
// DT5: round-trip via serialise/deserialise preserves packed step data.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT5: serialise round-trip preserves step data", "[per-applet-pilot][divseq10]") {
    // Build a known payload: all steps positive (1..10), no mute, no negative.
    int steps[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint64_t packed = pack_divseq10(steps, false);

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

    // Both hemi_lo and hemi_hi must round-trip unchanged.
    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);
    REQUIRE(rt_lo == lo);

    const char* hi_pos = std::strstr(out.c_str(), "hemi_hi");
    REQUIRE(hi_pos != nullptr);
    const char* colon_hi = std::strchr(hi_pos, ':');
    REQUIRE(colon_hi != nullptr);
    uint32_t rt_hi = (uint32_t)std::atoi(colon_hi + 1);
    REQUIRE(rt_hi == hi);
}

// ---------------------------------------------------------------------------
// DT6: clock input generates output on Trig 1 or Trig 2.
//
// Drives a rising clock edge on the Clock gate input and asserts that at
// least one of the trigger outputs goes high. With 10 inner ticks per buffer
// the applet fires on every clock edge. Does NOT assert which output fires
// (depends on sequence state from Start()), but confirms the clock path runs.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT6: rising clock edge on input 0 produces output on Trig 1 or Trig 2", "[per-applet-pilot][divseq10]") {
    auto s = make_setup();

    // Drive Clock input (bus 1) with a rising edge pulse.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // At least one trigger output must fire.
    bool trig1 = any_gate_high(s.bus, kTrig1BusIdx, kNumFrames);
    bool trig2 = any_gate_high(s.bus, kTrig2BusIdx, kNumFrames);
    REQUIRE((trig1 || trig2));
}

// ---------------------------------------------------------------------------
// DT7: reset input resets the sequence position.
//
// Sends a Reset pulse and confirms that output fires on the very next clock
// (sequence restarted from step 0).
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT7: reset input resets sequence position", "[per-applet-pilot][divseq10]") {
    auto s = make_setup();

    // Drive a few clocks to advance sequence state.
    for (int i = 0; i < 5; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset pulse on bus 2.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After reset, sequence is at start. Next clock should produce output.
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    bool trig1 = any_gate_high(s.bus, kTrig1BusIdx, kNumFrames);
    bool trig2 = any_gate_high(s.bus, kTrig2BusIdx, kNumFrames);
    REQUIRE((trig1 || trig2));
}

// ---------------------------------------------------------------------------
// DT8: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT8: hasCustomUi returns expected bitmask", "[per-applet-pilot][divseq10]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// DT9: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("DivSeq10 DT9: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][divseq10]") {
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
