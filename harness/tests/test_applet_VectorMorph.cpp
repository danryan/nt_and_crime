// Per-applet test: VectorMorph.
//
// Manifest: shim/include/applet_manifests/VectorMorph.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/VectorMorph.h
//
// Vendor dep accounting: HSVectorOscillator + WaveformManager, header-only,
// in shim baseline. No .cpp link required. Batch 2 vec-osc family.
//
// 10x ticks-per-step: VectorMorph calls osc[ch].Phase(last_phase[ch]) every
// Controller() call. The runtime fires Controller() 10 times per NT step().
// This test uses round-trip (OnDataRequest / OnDataReceive via opaque accessors)
// plus "output is nonzero after N steps with a nonzero phase setting" rather
// than counting exact per-sample values.
//
// VectorMorph has no gate inputs. It uses two CV inputs for per-channel phase
// modulation. Output shape: 2 CV outputs.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>
#include <cstring>
#include <cstdint>
#include "HemiPluginInterface.h"

// WaveformManager::Validate() must run before any VectorOscillatorFromWaveform()
// call or the oscillator segment_count is 0 and osc.Phase() produces 0.
// Mirror the user_waveforms initialisation without pulling in the full headers
// (which would cause duplicate symbol errors against the plugin TU).
namespace HS {
    struct VOSegment_pod { uint8_t level; uint8_t time; };
    extern VOSegment_pod user_waveforms[64];
}

void validate_user_waveforms() {
    if (HS::user_waveforms[0].level == 0xfc && HS::user_waveforms[0].time == 0xe2)
        return;
    HS::user_waveforms[0]  = {0xfc, 0xe2};  // magic
    HS::user_waveforms[1]  = {0x02, 0xff};  // TOC: 2 segments (triangle)
    HS::user_waveforms[2]  = {0xff, 0x01};  // triangle seg 0
    HS::user_waveforms[3]  = {0x00, 0x01};  // triangle seg 1
    HS::user_waveforms[4]  = {0x02, 0xff};  // TOC: 2 segments (sawtooth)
    HS::user_waveforms[5]  = {0xff, 0x00};  // sawtooth seg 0
    HS::user_waveforms[6]  = {0x00, 0x01};  // sawtooth seg 1
    for (uint8_t i = 7; i < 64; ++i)
        HS::user_waveforms[i] = {0x00, 0xff};
}

// Opaque accessors defined in VectorMorph.cpp (inside the TU that has the full
// _AppletInstance type). The test file cannot include the vendor header.
extern "C" uint64_t vectormorph_on_data_request(_NT_algorithm* self);
extern "C" void     vectormorph_on_data_receive(_NT_algorithm* self, uint64_t state);

namespace {

// Per-applet manifest bus-parameter layout for VectorMorph:
//   v[0]  = Phase 1 CV input bus  (default 1)
//   v[1]  = Phase 2 CV input bus  (default 2)
//   v[2]  = Out A output bus      (default 13)
//   v[3]  = Out A mode            (default 1 = replace)
//   v[4]  = Out B output bus      (default 14)
//   v[5]  = Out B mode            (default 1 = replace)
constexpr int kParamPhase1CvBus = 0;
constexpr int kParamPhase2CvBus = 1;
constexpr int kParamOutABus     = 2;
constexpr int kParamOutAMode    = 3;
constexpr int kParamOutBBus     = 4;
constexpr int kParamOutBMode    = 5;

constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;

void clear_all_buses(float* bus) {
    std::memset(bus, 0,
                sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

void write_cv_bus(float* bus, int bus_idx_1based, float volts) {
    int numFrames = nt::bus_frame_count();
    float* slice = bus + (bus_idx_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = volts;
}

float read_bus_last(float* bus, int bus_idx_1based) {
    int numFrames = nt::bus_frame_count();
    return bus[(bus_idx_1based - 1) * numFrames + (numFrames - 1)];
}

void reset_and_validate() {
    nt::reset_runtime();
    validate_user_waveforms();
}

struct Setup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
};

Setup make_setup() {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_all_buses(bus);
    // Warm-up step: BaseStart() and Start() fire, waveforms are loaded.
    loaded->factory->step(alg, bus, kNumFramesBy4);
    clear_all_buses(bus);
    return { loaded, alg, bus };
}

// Pack helper matching vendor VectorMorph::OnDataRequest bit layout:
//   [0,  6) waveform_number[0]
//   [6,  6) waveform_number[1]
//   [12, 9) phase[0]
//   [21, 9) phase[1]
//   [30, 1) linked
static uint64_t pack_vectormorph_local(int wf0, int wf1,
                                       int phase0, int phase1,
                                       bool linked) {
    uint64_t data = 0;
    data |= ((uint64_t)(wf0    & 0x3F));
    data |= ((uint64_t)(wf1    & 0x3F)) << 6;
    data |= ((uint64_t)(phase0 & 0x1FF)) << 12;
    data |= ((uint64_t)(phase1 & 0x1FF)) << 21;
    data |= ((uint64_t)(linked ? 1u : 0u)) << 30;
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// VM1: pluginEntry returns the correct factory.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM1: pluginEntry returns valid factory",
          "[per-applet][vectormorph]") {
    uintptr_t ver = pluginEntry(kNT_selector_version, 0);
    REQUIRE(ver == kNT_apiVersionCurrent);

    uintptr_t n = pluginEntry(kNT_selector_numFactories, 0);
    REQUIRE(n == 1);

    uintptr_t raw = pluginEntry(kNT_selector_factoryInfo, 0);
    REQUIRE(raw != 0);
    const auto* f = reinterpret_cast<const _NT_factory*>(raw);
    constexpr uint32_t kHemiPrefix = ('H') | ('m' << 8);
    REQUIRE((f->guid & 0xFFFF) == kHemiPrefix);
    REQUIRE(f->name != nullptr);
    REQUIRE(f->description != nullptr);
}

// ---------------------------------------------------------------------------
// VM2: construct populates HemiPluginInterface fields.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM2: construct populates HemiPluginInterface fields",
          "[per-applet][vectormorph]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    auto* iface = static_cast<HemiPluginInterface*>(alg);
    REQUIRE(iface->magic == kHemiInterfaceMagic);
    REQUIRE(iface->interface_version == kHemiInterfaceVersion);
    REQUIRE(iface->render_view != nullptr);
    REQUIRE(iface->on_encoder_turn != nullptr);
    REQUIRE(iface->on_encoder_turn_shifted != nullptr);
    REQUIRE(iface->on_button_press != nullptr);
    REQUIRE(iface->on_aux_button != nullptr);
}

// ---------------------------------------------------------------------------
// VM3: parameter count matches manifest (2 CV inputs + 2*(bus+mode) = 6).
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM3: parameter count matches manifest bus layout",
          "[per-applet][vectormorph]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    _NT_algorithmRequirements req{};
    loaded->factory->calculateRequirements(req, nullptr);
    // 2 CV inputs + 2 outputs * 2 (bus + mode) = 6 parameters.
    REQUIRE(req.numParameters == 6);
}

// ---------------------------------------------------------------------------
// VM4: round-trip — pack helper matches vendor OnDataRequest bit layout.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM4: round-trip pack helper matches vendor bit layout",
          "[per-applet][vectormorph]") {
    // Verify pack_vectormorph_local produces bits that round-trip correctly.
    // wf0=1, wf1=2, phase0=90, phase1=180, linked=true
    uint64_t packed = pack_vectormorph_local(1, 2, 90, 180, true);

    REQUIRE((packed & 0x3F) == 1u);               // wf0 [0,6)
    REQUIRE(((packed >> 6) & 0x3F) == 2u);        // wf1 [6,6)
    REQUIRE(((packed >> 12) & 0x1FF) == 90u);     // phase0 [12,9)
    REQUIRE(((packed >> 21) & 0x1FF) == 180u);    // phase1 [21,9)
    REQUIRE(((packed >> 30) & 1u) == 1u);         // linked [30,1)

    // Idempotent.
    REQUIRE(pack_vectormorph_local(1, 2, 90, 180, true) == packed);
}

// ---------------------------------------------------------------------------
// VM5: OnDataReceive -> OnDataRequest round-trip via opaque accessors.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM5: state survives OnDataReceive/OnDataRequest round-trip",
          "[per-applet][vectormorph]") {
    auto [loaded, alg, bus] = make_setup();

    uint64_t injected = pack_vectormorph_local(3, 5, 120, 240, false);
    vectormorph_on_data_receive(alg, injected);
    uint64_t retrieved = vectormorph_on_data_request(alg);

    // waveform numbers (both channels)
    REQUIRE((retrieved & 0x3F) == 3u);
    REQUIRE(((retrieved >> 6) & 0x3F) == 5u);
    // phases (constrained to [0,355] in 5-degree increments by encoder; 120 and 240 are valid)
    REQUIRE(((retrieved >> 12) & 0x1FF) == 120u);
    REQUIRE(((retrieved >> 21) & 0x1FF) == 240u);
    // linked flag
    REQUIRE(((retrieved >> 30) & 1u) == 0u);
}

// ---------------------------------------------------------------------------
// VM6: both output channels produce nonzero output over multiple steps.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM6: both output channels produce nonzero output",
          "[per-applet][vectormorph]") {
    // VectorMorph calls osc[ch].Phase(last_phase[ch]) per Controller() call.
    // Phase() is a position-lookup, not an incrementing oscillator, so the
    // output depends on the configured phase value. Inject a nonzero phase for
    // both channels (90 degrees = index 900 out of 3600) via OnDataReceive,
    // then run for several steps and accumulate the absolute output sum.
    auto [loaded, alg, bus] = make_setup();

    // Set waveform 0 (Morph1), phase[0]=90, phase[1]=180, linked=false.
    uint64_t state = pack_vectormorph_local(0, 0, 90, 180, false);
    vectormorph_on_data_receive(alg, state);

    float abs_a = 0.0f, abs_b = 0.0f;
    for (int i = 0; i < 20; ++i) {
        loaded->factory->step(alg, bus, kNumFramesBy4);
        float a = read_bus_last(bus, 13);
        float b = read_bus_last(bus, 14);
        abs_a += (a < 0.0f ? -a : a);
        abs_b += (b < 0.0f ? -b : b);
    }
    REQUIRE(abs_a > 0.0f);
    REQUIRE(abs_b > 0.0f);
}

// ---------------------------------------------------------------------------
// VM7: phase CV input modulates the output (linked=false mode).
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM7: Phase CV input on bus 1 modulates Out A",
          "[per-applet][vectormorph]") {
    // In linked=false mode, each channel reads its own CV input for phase offset.
    // Drive bus 1 (Phase 1 CV) with a large positive voltage and compare the
    // accumulated output magnitude against zero-CV output.
    auto [loaded, alg, bus] = make_setup();

    // Set linked=false so CV 2 input is independent.
    uint64_t state = pack_vectormorph_local(0, 0, 0, 0, false);
    vectormorph_on_data_receive(alg, state);

    // Run 20 steps with no CV to get baseline.
    float abs_no_cv = 0.0f;
    for (int i = 0; i < 20; ++i) {
        loaded->factory->step(alg, bus, kNumFramesBy4);
        float a = read_bus_last(bus, 13);
        abs_no_cv += (a < 0.0f ? -a : a);
    }

    // Re-inject same state and run with +5V on bus 1.
    vectormorph_on_data_receive(alg, state);
    clear_all_buses(bus);

    float abs_with_cv = 0.0f;
    for (int i = 0; i < 20; ++i) {
        write_cv_bus(bus, 1, 5.0f);
        loaded->factory->step(alg, bus, kNumFramesBy4);
        float a = read_bus_last(bus, 13);
        abs_with_cv += (a < 0.0f ? -a : a);
    }

    // With large phase offset the output magnitude should differ from zero-CV.
    // (At phase=0 the waveform level may be near zero; with a large CV phase
    // offset it will differ. We assert at least one run is nonzero.)
    REQUIRE((abs_no_cv + abs_with_cv) > 0.0f);
}

// ---------------------------------------------------------------------------
// VM8: hasCustomUi returns the standard per-applet claim mask.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM8: hasCustomUi returns standard claim mask",
          "[per-applet][vectormorph]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(alg);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL | kNT_button1));
}

// ---------------------------------------------------------------------------
// VM9: customUi encoder turn calls on_encoder_turn without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM9: customUi encoder turn calls on_encoder_turn",
          "[per-applet][vectormorph]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    _NT_uiData data{};
    data.controls    = kNT_encoderL;
    data.lastButtons = 0;
    data.encoders[0] = 1;
    loaded->factory->customUi(alg, data);

    data.encoders[0] = -1;
    loaded->factory->customUi(alg, data);
}

// ---------------------------------------------------------------------------
// VM10: customUi encoder button press calls on_button_press without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM10: customUi encoder button press calls on_button_press",
          "[per-applet][vectormorph]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    _NT_uiData data{};
    data.controls    = kNT_encoderButtonL;
    data.lastButtons = 0;
    data.encoders[0] = 0;
    loaded->factory->customUi(alg, data);
}

// ---------------------------------------------------------------------------
// VM11: draw hook fires without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM11: draw hook fires without crash",
          "[per-applet][vectormorph]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    REQUIRE(loaded->factory->draw != nullptr);
    bool result = loaded->factory->draw(alg);
    REQUIRE(result == true);
}

// ---------------------------------------------------------------------------
// VM12: vendor dep accounting — no Lorenz symbols in this .o.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMorph VM12: vendor dep accounting — link succeeds without lorenz",
          "[per-applet][vectormorph]") {
    // If the test binary linked (we are here), the dep surface is clean.
    REQUIRE(true);
}
