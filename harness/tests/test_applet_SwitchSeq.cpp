// Per-applet pilot test: SwitchSeq.
//
// Manifest: shim/include/applet_manifests/SwitchSeq.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/SwitchSeq.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. SwitchSeq's Controller calls
//   Advance() inside `if (Clock(0))`, so one bus-level rising edge advances
//   all four sequences 10 times per buffer.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection, drop
//   bus-level fire-count assertions). The full 32-step MiniSeq buffers do NOT
//   fit in the 64-bit hemi blob; only the scalar mode[0..1] fields round-trip.
//   Fidelity limit: OC::user_patterns storage is per-TU; programmed step data
//   does not survive preset save/reload (matches vendor EEPROM page reality
//   and the documented CHORDS user_chords precedent).

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for SwitchSeq manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (CV Ch 2) bus selector, default 2
//   v[2] = output 0 (Ch 1) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Ch 2) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx   = 1;   // default bus index for Clock input
constexpr int kOut1BusIdx    = 13;  // default bus index for Ch 1 output
constexpr int kNumFrames     = 32;
constexpr int kNumFramesBy4  = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single-sample gate pulse at frame 0 on the given 1-based bus.
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

// Local pack helper mirroring SwitchSeq::OnDataRequest byte-by-byte.
//   bits [0,  8) = mode[0] + 32
//   bits [8, 16) = mode[1] + 32
// mode range is -3..3, bias +32 so stored value is 29..35.
static uint64_t pack_switchseq(int mode0, int mode1) {
    uint64_t data = 0;
    data |= (uint64_t)((mode0 + 32) & 0xFF);
    data |= (uint64_t)((mode1 + 32) & 0xFF) << 8;
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// SS1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS1: pluginEntry returns factory with correct guid", "[per-applet-pilot][switchseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','S','s');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "SwitchSeq");
}

// ---------------------------------------------------------------------------
// SS2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][switchseq]") {
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
// SS3: round-trip via serialise/deserialise preserves mode[0] and mode[1].
//
// Only the scalar fields mode[0..1] are packed in the 64-bit blob; the full
// 32-step pattern buffers in OC::user_patterns are not persisted.
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS3: serialise round-trip preserves mode fields", "[per-applet-pilot][switchseq]") {
    // Verify local pack bit layout.
    uint64_t packed = pack_switchseq(2, -1);
    REQUIRE(((int)(packed       & 0xFF) - 32) == 2);   // mode[0]
    REQUIRE(((int)((packed >> 8) & 0xFF) - 32) == -1); // mode[1]
    REQUIRE((packed >> 16) == 0u);  // SwitchSeq uses only 16 bits; rest is 0

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
// SS4: pack helper covers default state (mode[0]=0, mode[1]=-1).
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS4: pack_switchseq encodes default state correctly", "[per-applet-pilot][switchseq]") {
    // Vendor Start() initialises mode[0]=0, mode[1]=-1.
    uint64_t packed = pack_switchseq(0, -1);
    REQUIRE(((int)(packed       & 0xFF) - 32) == 0);   // mode[0] default
    REQUIRE(((int)((packed >> 8) & 0xFF) - 32) == -1); // mode[1] default
    REQUIRE((packed >> 16) == 0u);
}

// ---------------------------------------------------------------------------
// SS5: round-trip preserves QUAN_MODE (-3) and RAND_MODE (-2).
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS5: round-trip preserves QUAN_MODE and RAND_MODE", "[per-applet-pilot][switchseq]") {
    uint64_t packed = pack_switchseq(-3, -2);
    REQUIRE(((int)(packed        & 0xFF) - 32) == -3);  // QUAN_MODE
    REQUIRE(((int)((packed >> 8) & 0xFF) - 32) == -2);  // RAND_MODE

    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    REQUIRE(loaded->factory->deserialise(loaded->algorithm, *parse));

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    const std::string& out = stream->buffer();

    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);
    REQUIRE(rt_lo == lo);
}

// ---------------------------------------------------------------------------
// SS6: clock input advances sequences without crashing.
//
// SHAPE 2 coverage: drives a rising clock edge and asserts the step() call
// completes without crash. Does NOT assert output value because the default
// OC::user_patterns storage initialises to zero, and mode[0]=0 (OCT mode)
// outputs a quantized note that may be 0V. The goal is to confirm the clock
// path through Advance() and ValueForChannel() executes without fault.
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS6: rising clock edge on input 0 runs without crash", "[per-applet-pilot][switchseq]") {
    auto s = make_setup();

    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    // Must not crash; sequences advance through MiniSeq::Advance().
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Serialise confirms the algorithm is still functional after the step.
    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SS7: reset input (Clock(1)) resets all sequences without crashing.
//
// The vendor Reset() calls miniseq[i].Reset() + Advance() for i in 0..3,
// so a gate on input 1 must not fault. SHAPE 2: assert step() completes.
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS7: reset input on bus 2 runs without crash", "[per-applet-pilot][switchseq]") {
    auto s = make_setup();

    // Advance a few times to put the sequences in a non-initial state.
    for (int i = 0; i < 3; ++i) {
        clear_bus(s.bus);
        pulse_bus(s.bus, kClockBusIdx, kNumFrames);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Drive reset on bus 2.
    clear_bus(s.bus);
    float* reset_slice = s.bus + (2 - 1) * kNumFrames;
    reset_slice[0] = 6.0f;
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SS8: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS8: hasCustomUi returns expected bitmask", "[per-applet-pilot][switchseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// SS9: customUi encoder turn and button press dispatch without crash.
// ---------------------------------------------------------------------------

TEST_CASE("SwitchSeq SS9: customUi encoder turn and button press dispatch without crash", "[per-applet-pilot][switchseq]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    // Encoder turn: OnEncoderMove changes mode[cursor].
    _NT_uiData data{};
    data.encoders[0]  = 1;
    data.controls     = 0;
    data.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, data);

    // Button press: OnButtonPress cycles cursor between 0 and 1.
    data.encoders[0]  = 0;
    data.controls     = kNT_encoderButtonL;
    data.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
