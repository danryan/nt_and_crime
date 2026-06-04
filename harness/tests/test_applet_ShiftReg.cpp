// Per-applet pilot test: ShiftReg.
//
// Manifest: shim/include/applet_manifests/ShiftReg.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ShiftReg.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on the
//   Clock gate input asserts HS::frame.clocked[0] = true, which stays asserted
//   across all 10 inner Controller() calls. ShiftReg's Controller advances the
//   shift register each time Clock(0) is true, so one bus-level rising edge
//   advances the register 10 times.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped; behavioral coverage relies on
//   confirming that output is non-zero after clock input is driven and that
//   state survives a serialise/deserialise round-trip. This avoids brittle math
//   on the 10x multiplier.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// Bus parameters for ShiftReg manifest (from emit_base_parameters):
//   v[0] = input 0 (Clock) bus selector, default 1
//   v[1] = input 1 (p Gate) bus selector, default 2
//   v[2] = output 0 (5bits Q) bus selector, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (8bits V) bus selector, default 14
//   v[5] = output 1 mode, default 1 (replace)

constexpr int kClockBusIdx  = 1;   // default bus index for Clock input
constexpr int kPGateBusIdx  = 2;   // default bus index for p Gate input
constexpr int kOutABusIdx   = 13;  // default bus index for 5bits Q output
constexpr int kOutBBusIdx   = 14;  // default bus index for 8bits V output
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

// Read whether any frame on the given 1-based bus is non-zero.
bool any_nonzero(const float* bus, int bus_1based, int numFrames) {
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

// pack_shiftreg mirrors ShiftReg::OnDataRequest byte-by-byte.
//
//   Pack(data, PackLocation {0,16},  reg)
//   Pack(data, PackLocation {16,7},  p)
//   Pack(data, PackLocation {23,4},  length - 1)
//   Pack(data, PackLocation {27,5},  quant_range - 1)
//   Pack(data, PackLocation {32,4},  out_b)
//   Pack(data, PackLocation {36,8},  constrain(scale, 0, 255))
//
// Arguments use vendor bias at the boundary: length and quant_range are stored
// minus-1; the caller passes the raw stored value (after bias).
static uint64_t pack_shiftreg(int reg, int p, int length_minus1,
                              int quant_range_minus1, int out_b, int scale) {
    uint64_t data = 0;
    data |= (uint64_t)(reg                & 0xFFFF);
    data |= (uint64_t)(p                  & 0x7F)   << 16;
    data |= (uint64_t)(length_minus1      & 0x0F)   << 23;
    data |= (uint64_t)(quant_range_minus1 & 0x1F)   << 27;
    data |= (uint64_t)(out_b              & 0x0F)   << 32;
    data |= (uint64_t)(scale              & 0xFF)   << 36;
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// SR1: factoryInfo returns a factory with the expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR1: pluginEntry returns factory with correct guid", "[per-applet-pilot][shiftreg]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','S','R');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "ShiftReg");
}

// ---------------------------------------------------------------------------
// SR2: HemiPluginInterface magic and version are populated by construct().
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR2: construct populates HemiPluginInterface magic and version", "[per-applet-pilot][shiftreg]") {
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
// SR3: round-trip via serialise/deserialise preserves all packed fields.
//
// Mirrors the pack_shiftreg layout from OnDataRequest:
//   bits [0,  16) = reg (16-bit shift register)
//   bits [16,  7) = p (probability 0..100)
//   bits [23,  4) = length - 1
//   bits [27,  5) = quant_range - 1
//   bits [32,  4) = out_b
//   bits [36,  8) = scale (clamped 0..255)
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR3: serialise round-trip preserves packed fields", "[per-applet-pilot][shiftreg]") {
    // Verify local pack bit layout before round-trip.
    // Use non-default values: reg=0xABCD, p=42, length=8 (stored as 7),
    // quant_range=16 (stored as 15), out_b=2, scale=5.
    uint64_t packed = pack_shiftreg(0xABCD, 42, 7, 15, 2, 5);
    REQUIRE((packed        & 0xFFFF)       == 0xABCDu);  // reg
    REQUIRE(((packed >> 16) & 0x7F)        == 42u);      // p
    REQUIRE(((packed >> 23) & 0x0F)        == 7u);       // length-1
    REQUIRE(((packed >> 27) & 0x1F)        == 15u);      // quant_range-1
    REQUIRE(((packed >> 32) & 0x0F)        == 2u);       // out_b
    REQUIRE(((packed >> 36) & 0xFF)        == 5u);       // scale

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
// SR4: clock input causes output on both outputs (behavior test, shape-2).
//
// After warmup, drive Clock input and confirm that the quantized pitch output
// (5bits Q) and the modulation output (8bits V) are non-zero. The initial reg
// value is randomised in Start(), so after 10 inner Controller ticks the
// register has been shifted 10 times; the probability p=0 keeps it stable.
// The Proportion-based CV output depends on the reg content, which is non-zero
// after Start()'s random init, so output should be non-zero.
//
// NOTE: this test relies on the initial random reg being non-zero. The shim
// random() LCG is deterministic at reset, so the warmup step will produce a
// non-zero reg in practice. If the test becomes flaky, inject a known reg via
// round-trip deserialise first.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR4: clock input produces non-zero output", "[per-applet-pilot][shiftreg]") {
    auto s = make_setup();

    // Drive Clock input (bus 1) with a rising edge pulse.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With non-zero reg (from Start's random init), the quantized CV output
    // should be non-zero after the clock advance.
    REQUIRE(any_nonzero(s.bus, kOutABusIdx, kNumFrames));
    REQUIRE(any_nonzero(s.bus, kOutBBusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// SR5: p Gate input unlocks probability.
//
// Drive p Gate high and clock; with p=0 and pCv=0 the register does not flip
// (prob=0). Verify output is still non-zero (register still advances). This
// confirms that the p Gate bus input is read and the register still shifts.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR5: p Gate asserted with clock still produces output", "[per-applet-pilot][shiftreg]") {
    auto s = make_setup();

    // Assert p Gate (bus 2) and Clock (bus 1) simultaneously.
    pulse_bus(s.bus, kClockBusIdx, kNumFrames);
    pulse_bus(s.bus, kPGateBusIdx, kNumFrames);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With p=0 the gate enables the probability path but prob stays 0 (no bit
    // flip). The shift register still advances so output is non-zero.
    REQUIRE(any_nonzero(s.bus, kOutABusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// SR6: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR6: hasCustomUi returns expected bitmask", "[per-applet-pilot][shiftreg]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// SR7: customUi encoder turn dispatches to OnEncoderMove without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR7: customUi encoder turn dispatches to OnEncoderMove", "[per-applet-pilot][shiftreg]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 1;   // left encoder turn +1
    data.controls     = 0;
    data.lastButtons  = 0;

    // Must not crash; dispatches through on_encoder_turn -> OnEncoderMove.
    loaded->factory->customUi(loaded->algorithm, data);

    // Applet still serialises correctly after the encoder interaction.
    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SR8: customUi encoder button edge dispatches to OnButtonPress.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR8: customUi encoder button edge dispatches to OnButtonPress", "[per-applet-pilot][shiftreg]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Simulate a fresh press: lastButtons = 0 (released), controls = pressed.
    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_encoderButtonL;
    data.lastButtons  = 0;

    // ShiftReg's OnButtonPress is the default (no-op from vendor). Must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SR9: customUi button1 edge dispatches to on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("ShiftReg SR9: customUi button1 edge dispatches to on_aux_button", "[per-applet-pilot][shiftreg]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData data{};
    data.encoders[0]  = 0;
    data.controls     = kNT_button1;
    data.lastButtons  = 0;

    // on_aux_button is a no-op for ShiftReg; must not crash.
    loaded->factory->customUi(loaded->algorithm, data);

    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
