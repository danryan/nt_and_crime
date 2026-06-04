// Per-applet pilot test: TB3PO.
//
// Manifest: shim/include/applet_manifests/TB3PO.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/TB3PO.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. TB_3PO advances step, fires pitch
//   and gate logic inside if (Clock(0)), so one bus-level edge fires 10 times
//   per buffer.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped; behavioral coverage relies on
//   confirming that output is generated (gate high, pitch non-zero) after clock
//   input is driven, and that state survives a round-trip serialise/deserialise.
//   The 10x multiplier makes step-count math unreliable at the bus level.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for TB3PO manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (Regen) bus selector, default 2
//   v[2] = output 0 (Pitch) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Gate) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus index for Clock input
constexpr int kRegenBusIdx  = 2;   // default bus index for Regen input
constexpr int kPitchBusIdx  = 13;  // default bus index for Pitch output
constexpr int kGateBusIdx   = 14;  // default bus index for Gate output
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

// Read whether any frame on a cv bus is non-zero.
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
    // One warmup step to let BaseStart settle (populates regen state).
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

// ---------------------------------------------------------------------------
// Local pack helper mirroring TB_3PO::OnDataRequest byte-by-byte.
//
// OnDataRequest layout (64-bit data word):
//   bit  0 (1 bit): lock_seed
//   bit  1 (1 bit): transpose_in_semitones
//   bits 2-11:      (gap, unused)
//   bits 12-15 (4 bits): density_encoder
//   bits 16-31 (16 bits): seed
//   bits 32-39:     (gap, unused)
//   bits 40-44 (5 bits): num_steps - 1  (stored as num_steps - 1)
//   bits 45-47:     (gap, unused)
//   bits 48-51 (4 bits): qselect
//   bit  52 (1 bit): hold_pitch
//
// Parameters match the OnDataRequest Pack calls directly. Gap bits are zeroed.
// ---------------------------------------------------------------------------

static uint64_t pack_tb3po(int lock_seed, int transpose_in_semitones,
                            int density_encoder, int seed,
                            int num_steps, int qselect, int hold_pitch) {
    uint64_t data = 0;
    data |= (uint64_t)(lock_seed          & 0x1);
    data |= (uint64_t)(transpose_in_semitones & 0x1) << 1;
    // bits 2-11 gap: zero
    data |= (uint64_t)(density_encoder    & 0xF) << 12;
    data |= (uint64_t)(seed               & 0xFFFF) << 16;
    // bits 32-39 gap: zero
    data |= (uint64_t)((num_steps - 1)    & 0x1F) << 40;
    // bits 45-47 gap: zero
    data |= (uint64_t)(qselect            & 0xF) << 48;
    data |= (uint64_t)(hold_pitch         & 0x1) << 52;
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// TB1: pluginEntry returns factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB1: pluginEntry returns factory with correct guid", "[per-applet-pilot][tb3po]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','T','3');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "TB-3PO");
}

// ---------------------------------------------------------------------------
// TB2: construct populates HemiPluginInterface magic and version.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][tb3po]") {
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
// TB3: round-trip via serialise/deserialise preserves all packed fields.
//
// Uses local pack_tb3po helper mirroring OnDataRequest bit layout.
// Verifies hemi_lo survives unchanged after deserialise + re-serialise.
// Gap bits 2-11, 32-39, 45-47 are zero in the packed word; OnDataReceive
// does not write them so the round-trip is stable.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB3: serialise round-trip preserves packed fields", "[per-applet-pilot][tb3po]") {
    // lock_seed=1, transpose=0, density=7, seed=0xABCD,
    // num_steps=16, qselect=0, hold_pitch=1
    uint64_t packed = pack_tb3po(1, 0, 7, 0xABCD, 16, 0, 1);

    // Verify bit layout before round-trip.
    REQUIRE((packed & 0x1) == 1u);                          // lock_seed
    REQUIRE(((packed >> 1) & 0x1) == 0u);                   // transpose
    REQUIRE(((packed >> 12) & 0xF) == 7u);                  // density
    REQUIRE(((packed >> 16) & 0xFFFF) == 0xABCDu);          // seed
    REQUIRE(((packed >> 40) & 0x1F) == 15u);                // num_steps-1 = 15
    REQUIRE(((packed >> 48) & 0xF) == 0u);                  // qselect
    REQUIRE(((packed >> 52) & 0x1) == 1u);                  // hold_pitch

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

    const char* hi_pos = std::strstr(out.c_str(), "hemi_hi");
    REQUIRE(hi_pos != nullptr);
    const char* hi_colon = std::strchr(hi_pos, ':');
    REQUIRE(hi_colon != nullptr);
    uint32_t rt_hi = (uint32_t)std::atoi(hi_colon + 1);
    REQUIRE(rt_hi == hi);
}

// ---------------------------------------------------------------------------
// TB4: rising clock edge on Clock input produces Gate output high.
//
// Shape-2 coverage: after a warmup step (which seeds the regen state),
// a clock edge is driven. The 10x multiplier means the applet's Controller
// fires 10 times; the pattern regenerated by Start() should contain gated
// steps. Asserts gate output is high at least once.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB4: rising clock edge produces gate output", "[per-applet-pilot][tb3po]") {
    auto s = make_setup();

    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With a seeded pattern (default density=12, num_steps=16), gated steps
    // exist in the generated pattern. At least one of the 10 inner Controller
    // ticks should have set curr_gate_cv > 0 and written it to the Gate bus.
    REQUIRE(any_gate_high(s.bus, kGateBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TB5: Pitch output is non-zero after a clock edge.
//
// TB_3PO::Start() calls Reset() which calls reseed() and regenerate_all().
// After warmup, curr_pitch_cv is set from get_pitch_for_step(). After a
// clock edge the applet writes curr_pitch_cv to Out(0). Asserts non-zero.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB5: pitch output is non-zero after clock edge", "[per-applet-pilot][tb3po]") {
    auto s = make_setup();

    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_cv_nonzero(s.bus, kPitchBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// TB6: Regen input (bus 2) triggers a pattern regeneration without crashing.
//
// Pulsing bus 2 (Regen) sets Clock(1) in Controller which calls Reset(),
// which calls reseed() when lock_seed==0. This exercises the regen path.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB6: regen input triggers regeneration without crash", "[per-applet-pilot][tb3po]") {
    auto s = make_setup();

    // Pulse regen bus to trigger Reset() -> reseed().
    pulse_bus(s.bus, kRegenBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Applet must still serialise correctly after regen.
    auto stream = nt::make_json_stream();
    s.loaded->factory->serialise(s.alg, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// TB7: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB7: hasCustomUi returns expected bitmask", "[per-applet-pilot][tb3po]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// TB8: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB8: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][tb3po]") {
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
// TB9: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("TB3PO TB9: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][tb3po]") {
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
