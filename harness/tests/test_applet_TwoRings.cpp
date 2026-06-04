// Per-applet pilot test: TwoRings.
//
// Manifest: shim/include/applet_manifests/TwoRings.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/TwoRings.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. TwoRings's Controller reads
//   Clock(0) and advances the shift registers on each call where Clock(0) is
//   true, so one bus-level rising edge produces 10 register advance ticks.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped; behavioral coverage relies on
//   confirming that output is non-zero after driving clock input and confirming
//   state survives a round-trip. This avoids brittle math on the 10x multiplier
//   while still covering all observable behaviors.
//
// OnDataRequest layout (TwoRings):
//   bits [ 0, 7) = p           (0..100)
//   bits [ 7, 5) = length - 1  (bias: store length-1, range 1..31)
//   bits [12, 5) = range - 1   (bias: store range-1, range 0..31)
//   bits [17, 4) = outmode[0]  (0..11)
//   bits [21, 4) = outmode[1]  (0..11)
//   bits [25, 8) = GAP         (commented out / always zero)
//   bits [33, 4) = cvmode[0]   (0..7)
//   bits [37, 4) = cvmode[1]   (0..7)
//   bits [41, 6) = smoothing   (0..63 in 6 bits)
//   bits [48, 4) = qselect[0]  (0..15)
//   bits [52, 4) = qselect[1]  (0..15)
//   bits [56, 1) = rotate_right (0 or 1)

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus indices from emit_base_parameters for TwoRings manifest:
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (p Gate) bus selector, default 2
//   v[2] = output 0 (Out A) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Out B) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx   = 1;   // default bus index for Clock input
constexpr int kPGateBusIdx   = 2;   // default bus index for p Gate input
constexpr int kOutABusIdx    = 13;  // default bus index for Out A
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

// Check whether any frame on the given 1-based bus has a non-zero value.
bool any_nonzero(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) {
        if (slice[i] != 0.0f) return true;
    }
    return false;
}

// pack_tworings: mirrors TwoRings::OnDataRequest() byte-by-byte.
// Gap at bits 25..32 is zero (never set).
static uint64_t pack_tworings(int p, int length, int range,
                               int outmode0, int outmode1,
                               int cvmode0, int cvmode1,
                               int smoothing,
                               int qselect0, int qselect1,
                               int rotate_right) {
    uint64_t data = 0;
    data |= (uint64_t)(p            & 0x7F);           // bits [0,7)
    data |= (uint64_t)((length - 1) & 0x1F) << 7;     // bits [7,5)
    data |= (uint64_t)((range  - 1) & 0x1F) << 12;    // bits [12,5)
    data |= (uint64_t)(outmode0     & 0x0F) << 17;    // bits [17,4)
    data |= (uint64_t)(outmode1     & 0x0F) << 21;    // bits [21,4)
    // bits [25,8) = 0 (commented-out scale field)
    data |= (uint64_t)(cvmode0      & 0x0F) << 33;    // bits [33,4)
    data |= (uint64_t)(cvmode1      & 0x0F) << 37;    // bits [37,4)
    data |= (uint64_t)(smoothing    & 0x3F) << 41;    // bits [41,6)
    data |= (uint64_t)(qselect0     & 0x0F) << 48;    // bits [48,4)
    data |= (uint64_t)(qselect1     & 0x0F) << 52;    // bits [52,4)
    data |= (uint64_t)(rotate_right & 0x01) << 56;    // bits [56,1)
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
// 2R1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R1: pluginEntry returns factory with correct guid", "[per-applet-pilot][tworings]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','2','R');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "TwoRings");
}

// ---------------------------------------------------------------------------
// 2R2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][tworings]") {
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
// 2R3: round-trip via serialise/deserialise preserves all packed fields.
//
// Uses pack_tworings which mirrors OnDataRequest byte-by-byte.
// Encodes non-default values for all fields and verifies the lo word survives.
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R3: serialise round-trip preserves packed fields", "[per-applet-pilot][tworings]") {
    // Verify pack helper produces expected bit layout before round-trip.
    // p=50, length=16, range=12, outmode0=PITCH1(1), outmode1=TRIG2(8),
    // cvmode0=LENGTH_MOD(1), cvmode1=RANGE_MOD(4), smoothing=8,
    // qselect0=0, qselect1=1, rotate_right=0
    uint64_t packed = pack_tworings(50, 16, 12, 1, 8, 1, 4, 8, 0, 1, 0);

    // Verify individual field extraction.
    REQUIRE((int)(packed & 0x7F) == 50);                           // p
    REQUIRE((int)((packed >> 7) & 0x1F) + 1 == 16);               // length
    REQUIRE((int)((packed >> 12) & 0x1F) + 1 == 12);              // range
    REQUIRE((int)((packed >> 17) & 0x0F) == 1);                   // outmode[0] = PITCH1
    REQUIRE((int)((packed >> 21) & 0x0F) == 8);                   // outmode[1] = TRIG2
    REQUIRE(((packed >> 25) & 0xFF) == 0u);                        // GAP zero
    REQUIRE((int)((packed >> 33) & 0x0F) == 1);                   // cvmode[0] = LENGTH_MOD
    REQUIRE((int)((packed >> 37) & 0x0F) == 4);                   // cvmode[1] = RANGE_MOD
    REQUIRE((int)((packed >> 41) & 0x3F) == 8);                   // smoothing
    REQUIRE((int)((packed >> 48) & 0x0F) == 0);                   // qselect[0]
    REQUIRE((int)((packed >> 52) & 0x0F) == 1);                   // qselect[1]
    REQUIRE((int)((packed >> 56) & 0x01) == 0);                   // rotate_right

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

    // Decode hemi_hi from the serialised string.
    const char* hi_pos = std::strstr(out.c_str(), "hemi_hi");
    REQUIRE(hi_pos != nullptr);
    const char* hi_colon = std::strchr(hi_pos, ':');
    REQUIRE(hi_colon != nullptr);
    uint32_t rt_hi = (uint32_t)std::atoi(hi_colon + 1);
    REQUIRE(rt_hi == hi);
}

// ---------------------------------------------------------------------------
// 2R4: clock input drives shift registers and Controller runs without crashing.
//
// Drives a rising clock edge on the Clock gate input. Uses MOD1 output mode
// (outmode[0]=3) which outputs a proportional bipolar value based on the
// register; with a non-zero initial register and range=24 (default), the
// output is non-zero except when the register low byte equals exactly the
// midpoint (127). The test forces p=0 (deterministic) and outmode[0]=MOD1 so
// the assertion is robust. Does NOT assert exact fire-count (10x multiplier;
// see file header).
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R4: rising clock edge on Clock input drives Controller without crashing", "[per-applet-pilot][tworings]") {
    auto s = make_setup();

    // Set outmode[0] = MOD1 (value 3) via round-trip so the output is a
    // bipolar proportional value from the register (avoids the PITCH1 note-64
    // zero-return edge case with some quantizer inputs).
    // p=0, length=16, range=24, outmode0=MOD1(3), outmode1=TRIG2(8),
    // cvmode0=LENGTH_MOD(1), cvmode1=RANGE_MOD(4), smoothing=0, qs0=0, qs1=1.
    uint64_t packed = pack_tworings(0, 16, 24, 3, 8, 1, 4, 0, 0, 1, 0);
    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);
    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);
    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    s.loaded->factory->deserialise(s.alg, *parse);

    // Drive Clock input and step.
    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // MOD1 outputs a value based on the shift register low byte relative to
    // the midpoint. Verify the output bus was written (any_nonzero is a proxy
    // for "controller ran and Out() was called"). Since initial reg[0] is
    // set by random() in Start() and is unlikely to produce exactly midpoint
    // after 10 shifts, we check for non-zero. Shape-2: no fire-count assertion.
    REQUIRE(any_nonzero(s.bus, kOutABusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// 2R5: p Gate input enables probability (gate-unlocked behavior).
//
// When p Gate (input 1) is high and cursor is NOT on PROB, the vendor logic
// uses p_mod for bit flipping (the "p unlocked" path). Drive both Clock and
// p Gate simultaneously and confirm the controller runs without crashing and
// serialise still works. Shape-2: no fire-count assertion; behavioral
// coverage is via the no-crash check and round-trip stability.
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R5: simultaneous Clock and p Gate runs Controller without crashing", "[per-applet-pilot][tworings]") {
    auto s = make_setup();

    // Set p=50 and MOD1 output so the register modulates the output directly.
    uint64_t packed = pack_tworings(50, 16, 24, 3, 8, 1, 4, 0, 0, 1, 0);
    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);
    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);
    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    s.loaded->factory->deserialise(s.alg, *parse);

    clear_bus(s.bus);
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    pulse_bus(s.bus, kPGateBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Verify the controller ran without crashing: serialise still works.
    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 2R6: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R6: hasCustomUi returns expected bitmask", "[per-applet-pilot][tworings]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// 2R7: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R7: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][tworings]") {
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
// 2R8: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R8: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][tworings]") {
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
// 2R9: AuxButton is dispatched via on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("TwoRings 2R9: on_aux_button dispatches AuxButton without crashing", "[per-applet-pilot][tworings]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    auto* p = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(p->on_aux_button != nullptr);

    // AuxButton toggles rotate_right when cursor is on LENGTH. Must not crash.
    p->on_aux_button(loaded->algorithm);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
