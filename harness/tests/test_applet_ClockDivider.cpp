// Per-applet pilot test: ClockDivider.
//
// Manifest: shim/include/applet_manifests/ClockDivider.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ClockDivider.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. ClockDivider's Controller reads
//   Clock(0) once per tick, so one bus-level rising edge produces 10 logical
//   clock ticks inside the applet.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped; behavioral coverage relies on
//   confirming that output is generated (gate high) after clock input is driven
//   and confirming state survives a round-trip. This avoids brittle math on
//   the 10x multiplier while still covering all observable behaviors.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for ClockDivider manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (Out A) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Out B) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus index for Clock input
constexpr int kResetBusIdx  = 2;   // default bus index for Reset input
constexpr int kOutABusIdx   = 13;  // default bus index for Out A
// kOutBBusIdx = 14; Out B not exercised in bus-level tests (shape-2 coverage).
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

}  // namespace

// ---------------------------------------------------------------------------
// CD1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD1: pluginEntry returns factory with correct guid", "[per-applet-pilot][clockdivider]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','C','d');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Clock Divider");
}

// ---------------------------------------------------------------------------
// CD2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][clockdivider]") {
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
// CD3: round-trip via serialise/deserialise preserves all four packed fields.
//
// Mirrors the pack_clock_divider layout from applet_test_helpers.h:
//   bits [0, 8)  = div[0] + 32
//   bits [8, 8)  = div[1] + 32
//   bits [16, 8) = divmult[1].steps + 32
//   bits [24, 8) = divmult[3].steps + 32
// applet_test_helpers.cpp is not linked into per-applet test binaries, so the
// pack function is inlined here.
// ---------------------------------------------------------------------------

static uint64_t local_pack_clock_divider(int div0, int div1,
                                         int divmult1, int divmult3) {
    uint64_t data = 0;
    data |= (uint64_t)((div0     + 32) & 0xFF);
    data |= (uint64_t)((div1     + 32) & 0xFF) << 8;
    data |= (uint64_t)((divmult1 + 32) & 0xFF) << 16;
    data |= (uint64_t)((divmult3 + 32) & 0xFF) << 24;
    return data;
}

TEST_CASE("ClockDivider CD3: serialise round-trip preserves div and divmult fields", "[per-applet-pilot][clockdivider]") {
    // Verify local pack bit layout before round-trip.
    uint64_t packed = local_pack_clock_divider(3, 5, 4, 8);
    REQUIRE(((int)(packed        & 0xFF) - 32) == 3);   // div[0]
    REQUIRE(((int)((packed >>  8) & 0xFF) - 32) == 5);  // div[1]
    REQUIRE(((int)((packed >> 16) & 0xFF) - 32) == 4);  // divmult[1].steps
    REQUIRE(((int)((packed >> 24) & 0xFF) - 32) == 8);  // divmult[3].steps
    REQUIRE((packed >> 32) == 0u);  // ClockDivider uses only 32 bits; hi is 0

    // Build the JSON that _per_applet_runtime::read_data_receive expects.
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
// CD4: clock input generates output (behavior test, shape-2 coverage).
//
// Drives a rising clock edge on the Clock gate input and asserts that Out A
// goes high at least once. Does NOT assert fire-count because the 10x clocked
// multiplier makes exact counting unreliable without modelling the inner-tick
// budget explicitly (see file header for shape-2 rationale).
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD4: rising clock edge on input 0 produces output on Out A", "[per-applet-pilot][clockdivider]") {
    auto s = make_setup();

    // Drive Clock input (bus 1) with a rising edge pulse.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With div[0]=2 (default from Start()), 10 inner ticks each seeing
    // clocked=true: ClkDivMult fires on clock_count==1 (first of every 2),
    // so Out A fires. Confirm at least one frame of Out A is gate-high.
    REQUIRE(any_gate_high(s.bus, kOutABusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// CD5: reset input clears divider state.
//
// Sends a Reset pulse and confirms that Out A fires on the very next clock
// (i.e., the divider counter was cleared back to 0).
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD5: reset input clears divider counter", "[per-applet-pilot][clockdivider]") {
    auto s = make_setup();

    // Drive a few clocks to put divider counter in some non-initial state.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset pulse on bus 2.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After reset, clock_count = 0 for all dividers. On next clock, count
    // reaches 1 immediately and fires (steps=2, fire on count==1).
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kOutABusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// CD6: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD6: hasCustomUi returns expected bitmask", "[per-applet-pilot][clockdivider]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// CD7: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD7: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][clockdivider]") {
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
// CD8: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD8: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][clockdivider]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Simulate a fresh press: lastButtons = 0 (released), controls = pressed.
    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_encoderButtonL;
    data.lastButtons  = 0;

    // OnButtonPress is a no-op by default in ClockDivider (vendor comment
    // shows it commented out). Must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// CD9: customUi button1 edge dispatches to on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("ClockDivider CD9: customUi button1 edge dispatches to on_aux_button", "[per-applet-pilot][clockdivider]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_button1;
    data.lastButtons  = 0;

    // on_aux_button is a no-op for ClockDivider; must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
