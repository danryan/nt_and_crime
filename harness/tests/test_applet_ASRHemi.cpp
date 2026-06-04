// Per-applet test: ASRHemi (Hemisphere ASR applet, build token ASRHemi).
//
// Manifest:    shim/include/applet_manifests/ASRHemi.h
// Vendor:      vendor/O_C-Phazerville/software/src/applets/ASR.h
// Vendor dep:  vendor/O_C-Phazerville/software/src/HSRingBufferManager.h
//              (header-only singleton, included transitively by ASR.h)
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on
//   Clock(0) sets HS::frame.clocked[0] = true; it stays asserted across all
//   10 inner Controller calls in that buffer. ASR calls StartADCLag() inside
//   if (Clock(0) && !secondary). Since Clock(0) stays true across all 10
//   ticks, StartADCLag resets the lag countdown on every tick, so
//   EndOfADCLag() NEVER fires within the same buffer as the clock edge.
//
// Single-shot gate / ADC-lag pattern (CLAUDE.md "Single-shot gate tests"):
//   To get EndOfADCLag to fire, the bus must be cleared after the clock-edge
//   step so that Clock(0) is false in the next buffer. The sequence is:
//     1. Write pulse on clock bus; call step() -- StartADCLag fires.
//     2. clear_bus() -- removes the clock edge.
//     3. call step() -- Clock(0) is false; lag counter decrements; if it
//        reaches 0 EndOfADCLag fires and the buffer is written / output
//        is quantized and emitted.
//
// Coverage shape: SHAPE 2 (round-trip + state injection plus the ADC-lag
//   single-shot pattern for output behaviour). Bus-level fire-count assertions
//   are dropped because the 10x multiplier makes exact counting unreliable.
//
// Pack helper layout (mirrors ASR::OnDataRequest byte-by-byte):
//   bits [ 0, 8) = buffer index (uint8, no bias)
//   bits [ 8, 8) = GetScale(0)  (uint8, no bias)
//   bits [16, 8) = GetScale(1)  (uint8, no bias)
//   bits [24,40) = 0            (unused / padding)
//
// Bus parameter layout (4-input manifest: 2 gate + 2 cv, 2 outputs):
//   v[0] = Clock gate bus,    default 1
//   v[1] = Freeze gate bus,   default 2
//   v[2] = CV cv bus,         default 3
//   v[3] = Index cv bus,      default 4
//   v[4] = Out A bus,         default 13
//   v[5] = Out A mode,        default 1 (replace)
//   v[6] = Out B bus,         default 14
//   v[7] = Out B mode,        default 1 (replace)

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Test seams defined in plugins/applets/ASRHemi.cpp.
uint64_t asrhemi_applet_on_data_request(_NT_algorithm* self);
void     asrhemi_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

namespace {

// Bus parameter layout (4-input manifest: 2 gate + 2 cv, 2 outputs):
//   v[0] = Clock gate bus,    default 1
//   v[1] = Freeze gate bus,   default 2
//   v[2] = CV cv bus,         default 3
//   v[3] = Index cv bus,      default 4
//   v[4] = Out A bus,         default 13
//   v[5] = Out A mode,        default 1 (replace)
//   v[6] = Out B bus,         default 14
//   v[7] = Out B mode,        default 1 (replace)
constexpr int kBusClock   = 1;   // v[0] default: Clock gate input
constexpr int kBusFreeze  = 2;   // v[1] default: Freeze gate input
constexpr int kBusCV      = 3;   // v[2] default: CV cv input
constexpr int kBusIndex   = 4;   // v[3] default: Index cv input
constexpr int kBusOutA    = 13;  // v[4] default: Out A
// kBusOutB = 14 (v[6] default: Out B) -- not used in behavioral tests.
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single-sample rising-edge pulse at frame 0 on a 1-based bus.
void pulse_bus(float* bus, int bus_1based) {
    bus[(bus_1based - 1) * kNumFrames] = 6.0f;
}

// Write a constant CV value across all frames of a 1-based bus.
void write_cv(float* bus, int bus_1based, float volts) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Read any non-zero value on a 1-based output bus (gate or CV).
bool any_nonzero(const float* bus, int bus_1based) {
    const float* src = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) {
        if (src[i] != 0.0f) return true;
    }
    return false;
}

// Local pack helper mirroring ASR::OnDataRequest byte-by-byte.
//   bits [ 0, 8) = index (uint8, no bias)
//   bits [ 8, 8) = scale_ch0 (uint8, no bias)
//   bits [16, 8) = scale_ch1 (uint8, no bias)
static uint64_t pack_asr(int index, int scale_ch0, int scale_ch1) {
    uint64_t data = 0;
    data |= (uint64_t)(index     & 0xFF);
    data |= (uint64_t)(scale_ch0 & 0xFF) <<  8;
    data |= (uint64_t)(scale_ch1 & 0xFF) << 16;
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
    // One warmup step to let BaseStart settle (registers the singleton).
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// AS1: factory has expected guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS1: pluginEntry returns factory with correct guid and name",
          "[per-applet-pilot][asrhemi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','A','s');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "ASR");
}

// ---------------------------------------------------------------------------
// AS2: construct populates HemiPluginInterface magic and version.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS2: construct populates HemiPluginInterface magic and version",
          "[per-applet-pilot][asrhemi]") {
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
// AS3: pack helper round-trips index and scale fields correctly.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS3: pack helper encodes and decodes fields without overlap",
          "[per-applet-pilot][asrhemi]") {
    uint64_t packed = pack_asr(5, 3, 7);
    REQUIRE((int)(packed        & 0xFF) == 5);   // index
    REQUIRE((int)((packed >>  8) & 0xFF) == 3);  // scale ch0
    REQUIRE((int)((packed >> 16) & 0xFF) == 7);  // scale ch1
    REQUIRE((packed >> 24) == 0u);               // upper bits unused
}

// ---------------------------------------------------------------------------
// AS4: serialise round-trip preserves index and scale fields.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS4: serialise round-trip preserves index and scale fields",
          "[per-applet-pilot][asrhemi]") {
    // index=3, scale_ch0=2, scale_ch1=4
    uint64_t packed = pack_asr(3, 2, 4);
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

    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);
    REQUIRE(rt_lo == lo);
}

// ---------------------------------------------------------------------------
// AS5: state-inject round-trip: index and scales survive via test seams.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS5: OnDataRequest/OnDataReceive round-trip preserves fields",
          "[per-applet-pilot][asrhemi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // index=10, scale_ch0=5, scale_ch1=9
    uint64_t original = pack_asr(10, 5, 9);
    asrhemi_applet_on_data_receive(alg, original);
    uint64_t readback = asrhemi_applet_on_data_request(alg);

    REQUIRE((int)(readback        & 0xFF) == 10);  // index
    REQUIRE((int)((readback >>  8) & 0xFF) == 5);  // scale ch0
    REQUIRE((int)((readback >> 16) & 0xFF) == 9);  // scale ch1
}

// ---------------------------------------------------------------------------
// AS6: clock edge followed by lag drain produces non-zero output.
//
// Single-shot gate pattern (CLAUDE.md "Single-shot gate tests"):
//   Step 1: pulse clock bus -> StartADCLag fires (sets countdown=96).
//           clocked[0] stays asserted for all 10 inner Controller ticks so
//           StartADCLag is re-called each tick, keeping countdown at 96
//           throughout the buffer; EndOfADCLag does NOT fire in this buffer.
//   Steps 2..N: clear bus -> Clock(0)=false; each step decrements countdown
//               by 10 (one per inner tick). After 10 no-clock steps the
//               countdown reaches 0 and EndOfADCLag fires, advancing the
//               ring buffer and writing the quantized CV to outputs.
//
// HEMISPHERE_ADC_LAG=96, 10 inner ticks/step -> 10 no-clock steps needed.
// Run 12 steps to give headroom. CV is held at 1.0 V throughout so the
// buffer accumulates a non-zero value to quantize and emit.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS6: clock edge plus ADC-lag drain produces non-zero output",
          "[per-applet-pilot][asrhemi]") {
    auto s = make_setup();

    // Write 1.0 V on the CV input bus (bus 3) so the ring buffer gets a real value.
    write_cv(s.bus, kBusCV, 1.0f);

    // Step 1: drive clock rising edge on the Clock gate bus (bus 1).
    pulse_bus(s.bus, kBusClock);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Clear pulse so Clock(0)=false going forward; keep CV alive on bus 3.
    clear_bus(s.bus);
    write_cv(s.bus, kBusCV, 1.0f);

    // Drain ADC lag: HEMISPHERE_ADC_LAG=96 / 10 inner ticks per step = 10 steps.
    // Run 12 to give one buffer of headroom.
    for (int i = 0; i < 12; ++i) {
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    // Out A (default bus 13) should carry a quantized non-zero value.
    REQUIRE(any_nonzero(s.bus, kBusOutA));
}

// ---------------------------------------------------------------------------
// AS7: without a clock edge the output is stable regardless of CV changes.
//
// The ring buffer only advances on a clock -> ADC-lag -> EndOfADCLag path.
// If no clock edge is driven, buffer_m.Advance() never runs, position stays
// fixed, and ReadValue always returns the same slot. Driving a different CV
// without a clock edge should produce identical output frames.
//
// Also exercises Freeze gate: confirm Freeze gate raised without crash.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS7: no clock edge means output is stable across CV changes",
          "[per-applet-pilot][asrhemi]") {
    auto s = make_setup();

    // Establish a baseline output with a stable CV and no clock.
    write_cv(s.bus, kBusCV, 1.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    float out_baseline = s.bus[(kBusOutA - 1) * kNumFrames + (kNumFrames - 1)];

    // Drive a different CV (still no clock), assert output unchanged.
    clear_bus(s.bus);
    write_cv(s.bus, kBusCV, 2.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    float out_after_cv = s.bus[(kBusOutA - 1) * kNumFrames + (kNumFrames - 1)];
    REQUIRE(out_after_cv == out_baseline);

    // Drive Freeze gate high with no clock -- confirm no crash.
    clear_bus(s.bus);
    write_cv(s.bus, kBusFreeze, 6.0f);
    write_cv(s.bus, kBusCV, 3.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    float out_frozen = s.bus[(kBusOutA - 1) * kNumFrames + (kNumFrames - 1)];
    REQUIRE(out_frozen == out_baseline);
}

// ---------------------------------------------------------------------------
// AS8: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS8: hasCustomUi returns expected bitmask",
          "[per-applet-pilot][asrhemi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// AS9: customUi encoder turn and button dispatch without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("ASRHemi AS9: customUi encoder turn and button dispatch without crashing",
          "[per-applet-pilot][asrhemi]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Encoder turn +1 (dispatches to OnEncoderMove).
    _NT_uiData data{};
    data.encoders[0] = 1;
    data.controls    = 0;
    data.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, data);

    // Encoder button press (dispatches to OnButtonPress).
    _NT_uiData data2{};
    data2.encoders[0]  = 0;
    data2.controls     = kNT_encoderButtonL;
    data2.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, data2);

    // Plug-in still serialises correctly after interactions.
    auto stream = nt::make_json_stream();
    loaded->factory->serialise(loaded->algorithm, *stream);
    REQUIRE(stream->buffer().find("hemi_lo") != std::string::npos);
}
