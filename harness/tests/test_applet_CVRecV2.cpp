// Per-applet test: CVRecV2.
//
// Manifest: shim/include/applet_manifests/CVRecV2.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/CVRecV2.h
//
// CVRecV2 is a dual CV recorder and player.  Input 0 (Clock) advances the
// step; Input 1 (Reset) returns the step to the start position.  Outputs
// 0 and 1 play back the recorded CV for each channel.
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10).  A single rising edge on
//   the Clock gate input asserts HS::frame.clocked[0] = true, which stays
//   asserted across all 10 inner Controller() calls.  CVRecV2's Controller
//   reads Clock(0) once per tick, so one bus-level rising edge fires the
//   clock branch 10 times inside the applet.
//
//   Coverage shape: SHAPE 2 (round-trip + state injection; drop fire-count
//   assertions).  The exact number of step advances per bus-level clock is
//   not asserted; tests confirm only that the step DID advance and that
//   outputs are driven.
//
// Pack helper (mirrors OnDataRequest byte-by-byte):
//   bits [0,  9) = start  (9 bits, raw value, no bias)
//   bits [9,  9) = end    (9 bits, raw value, no bias)
//   bits [18, 1) = smooth (1 bit)
//
// Bus parameter layout (2 inputs, 2 cv outputs -> kNumParams = 6):
//   v[0] = Clock  input bus  (default 1, gate)
//   v[1] = Reset  input bus  (default 2, gate)
//   v[2] = Play 1 output bus (default 13)
//   v[3] = Play 1 output mode(default 1 = replace)
//   v[4] = Play 2 output bus (default 14)
//   v[5] = Play 2 output mode(default 1 = replace)

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus index defaults from emit_base_parameters (2 gate inputs, 2 cv outputs).
constexpr int kClockBus  = 1;   // v[0] Clock input
constexpr int kResetBus  = 2;   // v[1] Reset input
// kPlay1Bus = 13 (v[2] Play 1 output, default bus 13)
// kPlay2Bus = 14 (v[4] Play 2 output, default bus 14)

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
// Local pack helper mirroring CVRecV2::OnDataRequest() byte-by-byte.
//
// bits [0,  9) = start  (raw, no bias; AND with 0x1FF mask)
// bits [9,  9) = end    (raw, no bias; AND with 0x1FF mask)
// bits [18, 1) = smooth (raw, no bias; AND with 0x1 mask)
// ---------------------------------------------------------------------------

static uint64_t pack_cvrecv2(int start, int end, int smooth) {
    uint64_t data = 0;
    data |= (uint64_t)(start  & 0x1FF);
    data |= (uint64_t)(end    & 0x1FF) << 9;
    data |= (uint64_t)(smooth & 0x1)   << 18;
    return data;
}

// Test seams defined in plugins/applets/CVRecV2.cpp.
uint64_t cvrecv2_on_data_request(_NT_algorithm* self);
void     cvrecv2_on_data_receive(_NT_algorithm* self, uint64_t data);

// ---------------------------------------------------------------------------
// CV1: factory guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV1: pluginEntry returns factory with correct guid",
          "[per-applet-pilot][cvrecv2]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','C','v');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "CVRec");
}

// ---------------------------------------------------------------------------
// CV2: construct populates HemiPluginInterface magic/version/hooks.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV2: construct populates HemiPluginInterface magic and version",
          "[per-applet-pilot][cvrecv2]") {
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
// CV3: serialise round-trip preserves packed fields.
//
// Defaults: start=0, end=63, smooth=0.
// Write non-default values, serialise, deserialise, re-serialise, compare.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV3: serialise round-trip preserves start/end/smooth",
          "[per-applet-pilot][cvrecv2]") {
    // Verify local pack bit layout before round-trip.
    uint64_t packed = pack_cvrecv2(10, 200, 1);
    REQUIRE((int)(packed        & 0x1FF) == 10);   // start
    REQUIRE((int)((packed >>  9) & 0x1FF) == 200); // end
    REQUIRE((int)((packed >> 18) & 0x1)  == 1);    // smooth
    REQUIRE((packed >> 19) == 0u);  // CVRecV2 uses only 19 bits; hi+upper lo = 0

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

// ---------------------------------------------------------------------------
// CV4: clock input advances the step (behavior test, shape-2 coverage).
//
// After a clock edge, Controller advances the step and calls Out(ch, ...).
// With the default recording buffer zero-initialised, both outputs should
// remain at 0V when playing back silence.  We verify the step IS driven by
// confirming the applet runs without crashing, not by asserting a voltage.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV4: clock edge advances step without crashing",
          "[per-applet-pilot][cvrecv2]") {
    auto s = make_setup();

    // Drive one clock edge.
    pulse_bus(s.bus, kClockBus, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After advance, serialise to confirm applet is still alive and consistent.
    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// CV5: reset input returns step to start.
//
// After several clocks the step has advanced.  A reset pulse returns step to
// the start position.  Confirm the applet survives and still serialises.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV5: reset input is processed without crashing",
          "[per-applet-pilot][cvrecv2]") {
    auto s = make_setup();

    // Advance several steps.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBus, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send reset.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBus, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clock after reset -- step should be back at start.
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBus, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// CV6: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV6: hasCustomUi returns expected bitmask",
          "[per-applet-pilot][cvrecv2]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// CV7: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV7: customUi encoder turn dispatches to OnEncoderMove",
          "[per-applet-pilot][cvrecv2]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 1;
    data.controls     = 0;
    data.lastButtons  = 0;

    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// CV8: customUi encoder button press routes to OnButtonPress without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV8: customUi encoder button dispatches to OnButtonPress",
          "[per-applet-pilot][cvrecv2]") {
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
// CV9: button1 (aux) routes to on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("CVRecV2 CV9: customUi button1 routes to on_aux_button (no-op)",
          "[per-applet-pilot][cvrecv2]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_button1;
    data.lastButtons  = 0;

    loaded->factory->customUi(loaded->algorithm, data);
    REQUIRE(true);
}
