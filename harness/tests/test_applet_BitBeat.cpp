// Per-applet pilot test: BitBeat.
//
// Manifest: shim/include/applet_manifests/BitBeat.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/BitBeat.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on a
//   Clock gate input asserts HS::frame.clocked[ch] = true, which stays
//   asserted across all 10 inner Controller() calls in that buffer.
//   BitBeat::Controller calls ProcessAlgorithm(ch, Clock(ch) ? GATE_RISING : 0)
//   once per inner tick per channel, so a single bus-level edge causes 10
//   calls to peaks::ByteBeat::ProcessSingleSample with gate_rising. In
//   free-running (stepmode off) bytebeat advances every sample regardless, so
//   the applet always produces output.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection; no exact
//   fire-count assertions on bus output). Output presence is asserted (any
//   non-zero frame), not exact count.
//
// OnDataRequest bit layout (vendor BitBeat.h):
//   bits [ 0, 4) = equation[0]         (4 bits, range 0..15)
//   bits [ 4, 4) = equation[1]         (4 bits, range 0..15)
//   bits [ 8, 8) = speed[0]            (8 bits, range 0..255)
//   bits [16, 8) = pitch[0]            (8 bits, range 1..255)
//   bits [24, 8) = p0[0]               (8 bits, range 0..255)
//   bits [32, 8) = p1[0]               (8 bits, range 0..255)
//   bits [40, 8) = p2[0]               (8 bits, range 0..255)
//   bits [48, 1) = stepmode[0]         (1 bit)
//   bits [49, 1) = loopmode[0]         (1 bit)
//   bits [50, 7) = loopstart[0] >> 1   (7 bits; recover: << 1)
//   bits [57, 7) = loopend[0] >> 1     (7 bits; recover: << 1)
//   Total: 64 bits, all fields used. No gap bits to zero.
//   Note: CV assignments, speed[1], pitch[1], p0..p2[1], stepmode[1],
//   loopmode[1], loopstart[1], loopend[1] are NOT persisted (insufficient
//   space in the 64-bit pack).

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus indices matching the manifest defaults (emit_base_parameters):
//   v[0] = input 0 (Reset1) bus selector, default 1
//   v[1] = input 1 (Reset2) bus selector, default 2
//   v[2] = output 0 (Beat1) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Beat2) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kBeat1BusIdx  = 13;
constexpr int kBeat2BusIdx  = 14;
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Read maximum absolute value on the given 1-based bus across all frames.
float bus_max_abs(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    float m = 0.0f;
    for (int i = 0; i < numFrames; ++i) {
        float v = slice[i] < 0.0f ? -slice[i] : slice[i];
        if (v > m) m = v;
    }
    return m;
}

// Read whether any frame on the given 1-based bus is non-zero (above 1e-6).
bool any_nonzero(const float* bus, int bus_1based, int numFrames) {
    return bus_max_abs(bus, bus_1based, numFrames) > 1e-6f;
}

// Pack helper: mirrors vendor BitBeat::OnDataRequest byte-by-byte.
// All fields use int at the boundary; no biases (raw values stored directly).
// No gap bits to zero (the 64-bit word is fully occupied at bit 63).
static uint64_t pack_bitbeat(int eq0, int eq1,
                              int speed0, int pitch0,
                              int p0_0, int p1_0, int p2_0,
                              int stepmode0, int loopmode0,
                              int loopstart0, int loopend0) {
    uint64_t d = 0;
    d |= (uint64_t)(eq0    & 0xF);               //  [0,4)
    d |= (uint64_t)(eq1    & 0xF) << 4;          //  [4,4)
    d |= (uint64_t)(speed0 & 0xFF) << 8;         //  [8,8)
    d |= (uint64_t)(pitch0 & 0xFF) << 16;        // [16,8)
    d |= (uint64_t)(p0_0   & 0xFF) << 24;        // [24,8)
    d |= (uint64_t)(p1_0   & 0xFF) << 32;        // [32,8)
    d |= (uint64_t)(p2_0   & 0xFF) << 40;        // [40,8)
    d |= (uint64_t)(stepmode0 & 0x1) << 48;      // [48,1)
    d |= (uint64_t)(loopmode0 & 0x1) << 49;      // [49,1)
    d |= (uint64_t)((loopstart0 >> 1) & 0x7F) << 50; // [50,7)
    d |= (uint64_t)((loopend0 >> 1)   & 0x7F) << 57; // [57,7)
    return d;
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
// BB1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB1: pluginEntry returns factory with correct guid", "[per-applet-pilot][bitbeat]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','B','b');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "BitBeat");
}

// ---------------------------------------------------------------------------
// BB2: construct populates HemiPluginInterface magic, version, and callbacks.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB2: construct populates HemiPluginInterface", "[per-applet-pilot][bitbeat]") {
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
// BB3: pack helper self-test -- verify the bit layout before round-trip.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB3: pack_bitbeat helper encodes and decodes correctly", "[per-applet-pilot][bitbeat]") {
    // Use non-default values to exercise each field.
    // loopstart/loopend are stored >> 1 so must be even for lossless round-trip.
    const int eq0 = 5, eq1 = 11;
    const int speed0 = 200, pitch0 = 128;
    const int p0_0 = 64, p1_0 = 100, p2_0 = 200;
    const int stepmode0 = 1, loopmode0 = 1;
    const int loopstart0 = 40, loopend0 = 200;  // both even

    uint64_t d = pack_bitbeat(eq0, eq1, speed0, pitch0,
                               p0_0, p1_0, p2_0,
                               stepmode0, loopmode0,
                               loopstart0, loopend0);

    REQUIRE((int)( d        & 0xF)  == eq0);
    REQUIRE((int)((d >>  4) & 0xF)  == eq1);
    REQUIRE((int)((d >>  8) & 0xFF) == speed0);
    REQUIRE((int)((d >> 16) & 0xFF) == pitch0);
    REQUIRE((int)((d >> 24) & 0xFF) == p0_0);
    REQUIRE((int)((d >> 32) & 0xFF) == p1_0);
    REQUIRE((int)((d >> 40) & 0xFF) == p2_0);
    REQUIRE((int)((d >> 48) & 0x1)  == stepmode0);
    REQUIRE((int)((d >> 49) & 0x1)  == loopmode0);
    REQUIRE((int)(((d >> 50) & 0x7F) << 1) == loopstart0);
    REQUIRE((int)(((d >> 57) & 0x7F) << 1) == loopend0);
}

// ---------------------------------------------------------------------------
// BB4: serialise round-trip preserves packed state.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB4: serialise round-trip preserves packed fields", "[per-applet-pilot][bitbeat]") {
    const int eq0 = 3, eq1 = 7;
    const int speed0 = 180, pitch0 = 50;
    const int p0_0 = 80, p1_0 = 120, p2_0 = 200;
    const int stepmode0 = 1, loopmode0 = 0;
    const int loopstart0 = 60, loopend0 = 180;

    uint64_t packed = pack_bitbeat(eq0, eq1, speed0, pitch0,
                                    p0_0, p1_0, p2_0,
                                    stepmode0, loopmode0,
                                    loopstart0, loopend0);

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
    REQUIRE(loaded->factory->deserialise(alg, *parse));

    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    loaded->factory->serialise(alg, *stream);
    const std::string& out = stream->buffer();

    // Decode hemi_lo and hemi_hi from output.
    const char* lo_pos  = std::strstr(out.c_str(), "hemi_lo");
    const char* hi_pos  = std::strstr(out.c_str(), "hemi_hi");
    REQUIRE(lo_pos != nullptr);
    REQUIRE(hi_pos != nullptr);

    const char* lo_col = std::strchr(lo_pos, ':');
    const char* hi_col = std::strchr(hi_pos, ':');
    REQUIRE(lo_col != nullptr);
    REQUIRE(hi_col != nullptr);

    uint32_t rt_lo = (uint32_t)std::atoi(lo_col + 1);
    uint32_t rt_hi = (uint32_t)std::atoi(hi_col + 1);

    REQUIRE(rt_lo == lo);
    REQUIRE(rt_hi == hi);
}

// ---------------------------------------------------------------------------
// BB5: step produces non-zero Beat1 output in free-running mode (stepmode off).
//
// In free-running mode peaks::ByteBeat advances t on every sample via the
// bytepitch counter. Equation 0 ("hope") at t=0..~1023 produces zero because
// its formula involves (t_>>10) which is 0 for small t. Once t exceeds 1024
// the output becomes non-zero. With default speed=255 and bytepitch_=1, t
// advances every inner Controller call, so ~1024 inner ticks are needed (each
// step() fires 10 inner ticks, so ~103 step calls suffice). 150 steps gives
// comfortable margin. This covers the peaks_bytebeat.cpp DSP link.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB5: step produces non-zero Beat1 output in free-running mode", "[per-applet-pilot][bitbeat]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);

    // Run enough steps for bytebeat t to advance past 1024 (the point where
    // equation 0 begins producing non-zero output). 150 steps * 10 inner
    // ticks = 1500 t increments.
    for (int i = 0; i < 150; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    // Beat1 output should be non-zero after enough t advancement.
    REQUIRE(any_nonzero(bus, kBeat1BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// BB6: step produces non-zero Beat2 output independently of Beat1.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB6: step produces non-zero Beat2 output", "[per-applet-pilot][bitbeat]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);

    // Beat2 uses equation[1] (default 1 per Start()). Equation 1 ("love"):
    // sample = (((((t_*pitch)*p0) & (t_>>4)) | ...) & 0xFF) -- needs t>>4 > 0,
    // so t > 15. 50 steps (500 inner ticks) is more than sufficient.
    for (int i = 0; i < 50; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    REQUIRE(any_nonzero(bus, kBeat2BusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// BB7: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB7: hasCustomUi returns expected bitmask", "[per-applet-pilot][bitbeat]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// BB8: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB8: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][bitbeat]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0] = 1;
    data.controls    = 0;
    data.lastButtons = 0;

    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// BB9: customUi encoder button dispatches to OnButtonPress without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("BitBeat BB9: customUi encoder button dispatches to OnButtonPress", "[per-applet-pilot][bitbeat]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0] = 0;
    data.controls    = kNT_encoderButtonL;
    data.lastButtons = 0;

    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
