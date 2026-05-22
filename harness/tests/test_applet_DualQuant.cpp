// Per-applet test: DualQuant.
//
// Manifest: shim/include/applet_manifests/DualQuant.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/DualQuant.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer.
//   DualQuant's Controller calls Clock(ch) which reads
//   HS::frame.clocked[ch]; one bus-level rising edge keeps clocked[ch]=true
//   across all 10 inner ticks. Each tick that sees Clock(ch)=true calls
//   StartADCLag(ch), resetting the lag timer, so EndOfADCLag never fires
//   within the same buffer. Quantization via the clocked path requires
//   multiple buffer steps.
//
//   Coverage shape: STANDARD (continuous mode, which bypasses the clock
//   path entirely). Continuous mode quantizes every inner tick via
//   `continuous[ch] || EndOfADCLag(ch)`. A clock edge disables continuous
//   mode; clocked-path tests use a warmup buffer with no clock to confirm
//   quantization runs before introducing a clock edge.
//
// Bus parameter layout (from emit_base_parameters, 4 inputs + 2 outputs):
//   v[0] = Clock 1 gate input bus,  default 1
//   v[1] = Clock 2 gate input bus,  default 2
//   v[2] = CV 1 input bus,          default 3
//   v[3] = CV 2 input bus,          default 4
//   v[4] = Pitch 1 output bus,      default 13
//   v[5] = Pitch 1 mode,            default 1
//   v[6] = Pitch 2 output bus,      default 14
//   v[7] = Pitch 2 mode,            default 1

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <cstdio>
#include <string>

// Test seams defined in plugins/applets/DualQuant.cpp.
uint64_t dualquant_applet_on_data_request(_NT_algorithm* self);
void     dualquant_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

namespace {

// Bus indices matching emit_base_parameters defaults for DualQuant.
constexpr int kBusClk1   = 1;   // v[0] default: Clock 1 gate input
constexpr int kBusCV1    = 3;   // v[2] default: CV 1 input
constexpr int kBusCV2    = 4;   // v[3] default: CV 2 input
constexpr int kBusPitch1 = 13;  // v[4] default: Pitch 1 output
constexpr int kBusPitch2 = 14;  // v[6] default: Pitch 2 output

constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a constant value in volts across all frames of a 1-based bus.
void write_cv(float* bus, int bus_1based, float volts) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a single-sample pulse at frame 0 on a 1-based gate bus.
void write_pulse(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    slice[0] = 6.0f;
}

// Read the last frame of a 1-based CV bus (in volts).
float read_cv_last(const float* bus, int bus_1based) {
    return bus[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
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
    // One warmup step to let BaseStart settle; no clock edges so continuous
    // mode stays active.
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// DQ1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ1: pluginEntry returns factory with correct guid", "[per-applet][dualquant]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','D','q');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "DualQuant");
}

// ---------------------------------------------------------------------------
// DQ2: construct populates HemiPluginInterface magic and version.
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ2: construct populates HemiPluginInterface magic and version", "[per-applet][dualquant]") {
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
// DQ3: OnDataRequest packs default scale and root=0 after Start.
//
// Vendor Start() leaves root at 0. The QuantEngine constructor initialises
// scale to OC::Scales::SCALE_SEMI (= 5; the chromatic semitone scale).
// DualQuant does not call SetScale in its own Start(), so the packed
// scale fields reflect the QuantEngine default.
// OnDataRequest packs:
//   bits [ 0, 8): GetScale(0)    = SCALE_SEMI = 5
//   bits [ 8, 8): GetScale(1)    = SCALE_SEMI = 5
//   bits [16, 4): GetRootNote(0) = 0
//   bits [20, 4): GetRootNote(1) = 0
// ---------------------------------------------------------------------------

static constexpr uint32_t kScaleSemi = 5u;  // OC::Scales::SCALE_SEMI

TEST_CASE("DualQuant DQ3: OnDataRequest packs default scale=SCALE_SEMI root=0 after Start", "[per-applet][dualquant]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t packed = dualquant_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF)         == kScaleSemi);  // scale 0 = SCALE_SEMI
    REQUIRE(((packed >> 8)  & 0xFF) == kScaleSemi);  // scale 1 = SCALE_SEMI
    REQUIRE(((packed >> 16) & 0x0F) == 0u);           // root 0
    REQUIRE(((packed >> 20) & 0x0F) == 0u);           // root 1
}

// ---------------------------------------------------------------------------
// DQ4: serialise round-trip preserves scale and root fields.
//
// Inject scale0=5, scale1=3, root0=2, root1=7 and confirm round-trip.
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ4: serialise round-trip preserves scale and root", "[per-applet][dualquant]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Pack: scale0=5, scale1=3, root0=2, root1=7
    uint64_t state_in = 0;
    state_in |= (uint64_t)(5u  & 0xFF);
    state_in |= (uint64_t)(3u  & 0xFF) << 8;
    state_in |= (uint64_t)(2u  & 0x0F) << 16;
    state_in |= (uint64_t)(7u  & 0x0F) << 20;

    dualquant_applet_on_data_receive(alg, state_in);

    uint32_t hi = (uint32_t)(state_in >> 32);
    uint32_t lo = (uint32_t)(state_in & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

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

    // Also verify via opaque accessor.
    uint64_t packed = dualquant_applet_on_data_request(alg);
    REQUIRE((packed & 0xFF)         == 5u);
    REQUIRE(((packed >> 8)  & 0xFF) == 3u);
    REQUIRE(((packed >> 16) & 0x0F) == 2u);
    REQUIRE(((packed >> 20) & 0x0F) == 7u);
}

// ---------------------------------------------------------------------------
// DQ5: continuous mode quantizes CV1 and outputs on Pitch 1.
//
// With scale=0 (chromatic, all semitones pass through), quantization is a
// no-op. CV1 input passes through to Pitch 1 output. Confirms the step
// path works end-to-end in continuous mode (no clock required).
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ5: continuous mode passes CV1 to Pitch 1 output", "[per-applet][dualquant]") {
    auto s = make_setup();

    // Write 2.0V on CV 1. With chromatic scale, output should be ~2.0V.
    write_cv(s.bus, kBusCV1, 2.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out = read_cv_last(s.bus, kBusPitch1);
    // Chromatic scale: output within 1/12 semitone (0.083V) of input.
    REQUIRE(out > 1.9f);
    REQUIRE(out < 2.1f);
}

// ---------------------------------------------------------------------------
// DQ6: continuous mode quantizes CV2 and outputs on Pitch 2.
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ6: continuous mode passes CV2 to Pitch 2 output", "[per-applet][dualquant]") {
    auto s = make_setup();

    write_cv(s.bus, kBusCV2, 3.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out = read_cv_last(s.bus, kBusPitch2);
    REQUIRE(out > 2.9f);
    REQUIRE(out < 3.1f);
}

// ---------------------------------------------------------------------------
// DQ7: clock edge disables continuous mode (clocked-path acknowledgement).
//
// After a clock edge, continuous[ch] = 0. The 10x inner ticks all see
// clocked=true and call StartADCLag (setting countdown=96) then
// EndOfADCLag (decrementing: 96->95, never reaching 0). So the first
// clocked buffer produces no quantized output. HEMISPHERE_ADC_LAG=96
// inner ticks are needed before EndOfADCLag fires; at 10 inner ticks per
// buffer that is ~10 buffers. Bus-level fire-count assertions on the
// clocked path are therefore unsound (CLAUDE.md shape-2 rationale).
//
// This test confirms the plug-in survives a clock-edge buffer without
// crashing. No output-value assertion is made on the clocked path.
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ7: clock edge on Ch1 does not crash", "[per-applet][dualquant]") {
    auto s = make_setup();

    // Provide CV on ch1 during the clocked step.
    write_cv(s.bus, kBusCV1, 2.0f);
    write_pulse(s.bus, kBusClk1);
    // Run the buffer; StartADCLag fires 10x, EndOfADCLag does not fire.
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    // Must not crash; output value not asserted (clocked path incomplete
    // within one buffer given HEMISPHERE_ADC_LAG=96 and 10 inner ticks).
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// DQ8: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ8: hasCustomUi returns expected bitmask", "[per-applet][dualquant]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// DQ9: encoder turn routes OnEncoderMove and changes scale selection.
//
// Default cursor=0 (scale for ch0). One encoder turn moves the scale
// selector and NudgeScale is called, which re-enables continuous[0]. The
// scale changes from default 0 (chromatic) to 1. Confirm packed scale
// field changes.
// ---------------------------------------------------------------------------

TEST_CASE("DualQuant DQ9: encoder turn changes scale via customUi", "[per-applet][dualquant]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Default scale for ch0 is SCALE_SEMI (= 5).
    uint64_t before = dualquant_applet_on_data_request(loaded->algorithm);
    REQUIRE((before & 0xFF) == kScaleSemi);

    // Encoder must be in edit mode to change scale. Press button first
    // to enter edit mode (OnButtonPress toggles EditMode).
    _NT_uiData ui_btn{};
    ui_btn.controls    = kNT_encoderButtonL;
    ui_btn.lastButtons = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui_btn);

    // Now turn the encoder clockwise: should call NudgeScale(0, +1).
    _NT_uiData ui_enc{};
    ui_enc.encoders[0] = 1;
    ui_enc.controls    = 0;
    ui_enc.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_enc);

    uint64_t after = dualquant_applet_on_data_request(loaded->algorithm);
    // Scale for ch0 should have advanced from 0 to 1.
    REQUIRE((after & 0xFF) != (before & 0xFF));
}
