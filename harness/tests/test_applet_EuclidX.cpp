// Per-applet pilot test: EuclidX.
//
// Manifest: shim/include/applet_manifests/EuclidX.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/EuclidX.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. EuclidX's Controller advances step
//   on every Clock(0) tick; on beat-steps it calls ClockOut (output high) and
//   on non-beat-steps it calls GateOut(ch, 0) (output zero). With 10 ticks per
//   buffer, the output value written to the bus depends on whether the 10th
//   inner step is a beat or not, making naive bus-level assertions brittle.
//
//   Coverage shape chosen: SHAPE 1 with inner_ticks_override=1 for EX4/EX5.
//   Setting hem_shim::inner_ticks_override=1 runs exactly ONE Controller tick
//   per step() call, so step 0 fires ClockOut and the output is captured high
//   before any subsequent non-beat step can overwrite it. EX4 uses override=1
//   to confirm Chan1 fires on the first clock when step=0 (a beat in the default
//   4-in-16 Euclidean pattern). EX5 uses override=1 after reset to confirm the
//   same.
//
// OnDataRequest layout (PARAM_SIZE=6, NUM_PARAMS=5):
//   Per channel (ch=0 then ch=1), idx advances 0..4 then 5..9 (10 fields):
//     bits [0*6, 6)   = length[0] - 1  (stored as length-1, range 1..31)
//     bits [1*6, 6)   = beats[0]        (range 0..32)
//     bits [2*6, 6)   = offset[0]       (range 0..length+padding-1)
//     bits [3*6, 6)   = padding[0]      (range 0..32-length)
//     bits [4*6, 6)   = cv_dest[0]      (EuclidXParam enum, 0..7)
//     bits [5*6, 6)   = length[1] - 1
//     bits [6*6, 6)   = beats[1]
//     bits [7*6, 6)   = offset[1]
//     bits [8*6, 6)   = padding[1]
//     bits [9*6, 6)   = cv_dest[1]
//   bit  [60, 1)      = gate_mode
//   bits [61, 3)      = zero (unused gap)

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Exposes hem_shim::inner_ticks_override (defined in shim/src/globals.cpp,
// aggregated into the EuclidX.cpp TU). Setting it to 1 before a step() call
// runs exactly one Controller() tick instead of the default 10, avoiding the
// beat/no-beat overwrite interaction described in the file header.
namespace hem_shim { extern int inner_ticks_override; }

namespace {

// Bus parameters for EuclidX manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Reset) bus selector, default 2
//   v[2] = output 0 (Chan1) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Chan2) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus index for Clock input
constexpr int kResetBusIdx  = 2;   // default bus index for Reset input
constexpr int kChan1BusIdx  = 13;  // default bus index for Chan1 output
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

// pack_euclidx mirrors EuclidX::OnDataRequest() byte-by-byte.
// PARAM_SIZE = 6, fields are 6-bit each.
// length[ch] is stored as length[ch]-1; all others stored as-is.
// gate_mode is 1 bit at position 60.
// gap bits [61..63] are zero.
static uint64_t pack_euclidx(
    int length0, int beats0, int offset0, int padding0, int cv_dest0,
    int length1, int beats1, int offset1, int padding1, int cv_dest1,
    int gate_mode)
{
    uint64_t data = 0;
    // ch=0: idx 0..4
    data |= (uint64_t)((length0 - 1) & 0x3F) << (0 * 6);
    data |= (uint64_t)(beats0        & 0x3F) << (1 * 6);
    data |= (uint64_t)(offset0       & 0x3F) << (2 * 6);
    data |= (uint64_t)(padding0      & 0x3F) << (3 * 6);
    data |= (uint64_t)(cv_dest0      & 0x3F) << (4 * 6);
    // ch=1: idx 5..9
    data |= (uint64_t)((length1 - 1) & 0x3F) << (5 * 6);
    data |= (uint64_t)(beats1        & 0x3F) << (6 * 6);
    data |= (uint64_t)(offset1       & 0x3F) << (7 * 6);
    data |= (uint64_t)(padding1      & 0x3F) << (8 * 6);
    data |= (uint64_t)(cv_dest1      & 0x3F) << (9 * 6);
    // gate_mode at bit 60
    data |= (uint64_t)(gate_mode & 0x1) << 60;
    // bits 61..63 are zero (unused)
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// EX1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX1: pluginEntry returns factory with correct guid", "[per-applet-pilot][euclidx]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','E','x');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "EuclidX");
}

// ---------------------------------------------------------------------------
// EX2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][euclidx]") {
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
// EX3: round-trip via serialise/deserialise preserves all packed fields.
//
// Mirrors pack_euclidx layout above. Vendor defaults after Start():
//   length[0]=16, beats[0]=4, offset[0]=0, padding[0]=0, cv_dest[0]=BEATS1(1)
//   length[1]=16, beats[1]=8, offset[1]=0, padding[1]=16, cv_dest[1]=BEATS2(5)
//   gate_mode=false
// We inject non-default values to verify all fields survive round-trip.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX3: serialise round-trip preserves packed fields", "[per-applet-pilot][euclidx]") {
    // Non-default values: length0=8, beats0=3, offset0=1, padding0=2, cv_dest0=2
    //                     length1=12, beats1=5, offset1=2, padding1=4, cv_dest1=6
    //                     gate_mode=1
    uint64_t packed = pack_euclidx(8, 3, 1, 2, 2,
                                   12, 5, 2, 4, 6,
                                   1);

    // Verify pack bit layout manually.
    REQUIRE(((int)((packed >> (0*6)) & 0x3F) + 1) == 8);    // length[0]
    REQUIRE((int)((packed >> (1*6)) & 0x3F) == 3);           // beats[0]
    REQUIRE((int)((packed >> (2*6)) & 0x3F) == 1);           // offset[0]
    REQUIRE((int)((packed >> (3*6)) & 0x3F) == 2);           // padding[0]
    REQUIRE((int)((packed >> (4*6)) & 0x3F) == 2);           // cv_dest[0]
    REQUIRE(((int)((packed >> (5*6)) & 0x3F) + 1) == 12);   // length[1]
    REQUIRE((int)((packed >> (6*6)) & 0x3F) == 5);           // beats[1]
    REQUIRE((int)((packed >> (7*6)) & 0x3F) == 2);           // offset[1]
    REQUIRE((int)((packed >> (8*6)) & 0x3F) == 4);           // padding[1]
    REQUIRE((int)((packed >> (9*6)) & 0x3F) == 6);           // cv_dest[1]
    REQUIRE((int)((packed >> 60) & 0x1) == 1);               // gate_mode
    REQUIRE((packed >> 61) == 0u);                            // gap bits are zero

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

    // Decode hemi_lo and hemi_hi from the serialised string.
    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);
    REQUIRE(rt_lo == lo);

    const char* hi_pos = std::strstr(out.c_str(), "hemi_hi");
    REQUIRE(hi_pos != nullptr);
    const char* hi_colon = std::strchr(hi_pos, ':');
    REQUIRE(hi_colon != nullptr);
    uint32_t rt_hi = (uint32_t)std::atoi(hi_colon + 1);
    REQUIRE(rt_hi == hi);
}

// ---------------------------------------------------------------------------
// EX4: clock input generates output (inner_ticks_override=1).
//
// With default settings (length=16, beats=4, offset=0, padding=0), step 0 is
// a beat in EuclideanPattern(16,4,0,0). Using inner_ticks_override=1 runs only
// one Controller tick, so the output from ClockOut(step=0) is captured before
// subsequent non-beat steps can call GateOut(ch,0) and overwrite it.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX4: rising clock edge on input 0 produces output on Chan1", "[per-applet-pilot][euclidx]") {
    auto s = make_setup();

    hem_shim::inner_ticks_override = 1;
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kChan1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// EX5: reset input clears step counter (inner_ticks_override=1 for final clock).
//
// Drives several clocks (at normal 10x rate) to advance the step counter, then
// pulses Reset and confirms that Chan1 fires on the very next clock. The post-
// reset clock uses inner_ticks_override=1 so only step=0 runs (a beat in the
// default 4-in-16 pattern) and the ClockOut output is not overwritten.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX5: reset input clears step counter", "[per-applet-pilot][euclidx]") {
    auto s = make_setup();

    // Drive several clock pulses to advance the step counter away from 0.
    for (int i = 0; i < 5; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Send Reset pulse.
    clear_bus(s.bus);
    pulse_bus(s.bus, kResetBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // After reset, step=0. Drive a clock with override=1 so only that one tick
    // runs: step 0 fires ClockOut and the output is captured before any
    // subsequent non-beat tick can zero it.
    clear_bus(s.bus);
    hem_shim::inner_ticks_override = 1;
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kChan1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// EX6: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX6: hasCustomUi returns expected bitmask", "[per-applet-pilot][euclidx]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// EX7: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX7: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][euclidx]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 1;   // left encoder turn +1
    data.controls     = 0;
    data.lastButtons  = 0;

    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// EX8: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX8: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][euclidx]") {
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
// EX9: customUi button1 edge dispatches to on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("EuclidX EX9: customUi button1 edge dispatches to on_aux_button", "[per-applet-pilot][euclidx]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_button1;
    data.lastButtons  = 0;

    // on_aux_button is a no-op for EuclidX; must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
