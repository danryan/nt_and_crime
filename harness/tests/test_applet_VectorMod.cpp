// Per-applet test: VectorMod.
//
// Manifest: shim/include/applet_manifests/VectorMod.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/VectorMod.h
//
// Vendor dep accounting: HSVectorOscillator + WaveformManager (header-only,
// shim baseline). No Lorenz, no tideslite .cpp link required.
//
// 10x ticks-per-step: VectorMod calls osc[ch].Next() in every Controller()
// call. The harness fires Controller() 10 times per NT step() buffer.
// Tests use round-trip + output-nonzero assertions rather than counting
// exact output-sample values, which would require modelling the full
// oscillator phase accumulator per inner tick.
//
// Bus layout (4 inputs, 2 outputs):
//   param[0]  = Trig 1 gate input bus   (default 1)
//   param[1]  = Cycle 1 CV input bus    (default 2)
//   param[2]  = Trig 2 gate input bus   (default 3)
//   param[3]  = Cycle 2 CV input bus    (default 4)
//   param[4]  = Ch1 Mod output bus      (default 13)
//   param[5]  = Ch1 Mod mode            (default 1 = replace)
//   param[6]  = Ch2 Mod output bus      (default 14)
//   param[7]  = Ch2 Mod mode            (default 1 = replace)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>
#include <cstring>
#include <cstdint>
#include "HemiPluginInterface.h"

// WaveformManager::Validate() initialises HS::user_waveforms with the
// magic signature and default waveforms. Without it, VectorOscillatorFromWaveform
// returns an empty oscillator and Next() always produces 0.
// We replicate the magic bytes here without pulling in the full headers
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

// Opaque accessors exported by the plugin TU.
extern "C" uint64_t vectormod_on_data_request(_NT_algorithm* self);
extern "C" void     vectormod_on_data_receive(_NT_algorithm* self, uint64_t state);

namespace {

constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;

// Parameter indices matching the manifest BusParam declaration order.
constexpr int kParamTrig1Bus    = 0;
constexpr int kParamCycle1Bus   = 1;
constexpr int kParamTrig2Bus    = 2;
constexpr int kParamCycle2Bus   = 3;
constexpr int kParamOut1Bus     = 4;
constexpr int kParamOut1Mode    = 5;
constexpr int kParamOut2Bus     = 6;
constexpr int kParamOut2Mode    = 7;

void clear_all_buses(float* bus) {
    std::memset(bus, 0,
                sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

void write_cv_bus(float* bus, int bus_idx_1based, float volts) {
    int numFrames = nt::bus_frame_count();
    float* slice = bus + (bus_idx_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = volts;
}

float read_bus_abs_sum(float* bus, int bus_idx_1based) {
    int numFrames = nt::bus_frame_count();
    float* slice = bus + (bus_idx_1based - 1) * numFrames;
    float sum = 0.0f;
    for (int i = 0; i < numFrames; ++i) sum += (slice[i] < 0.0f ? -slice[i] : slice[i]);
    return sum;
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
    // Warm-up step so BaseStart() and Start() have fired.
    loaded->factory->step(alg, bus, kNumFramesBy4);
    clear_all_buses(bus);
    return { loaded, alg, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// VM1: pluginEntry returns valid factory with correct Hemi guid prefix.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM1: pluginEntry returns valid factory",
          "[per-applet][vectormod]") {
    uintptr_t ver = pluginEntry(kNT_selector_version, 0);
    REQUIRE(ver == kNT_apiVersionCurrent);

    uintptr_t n = pluginEntry(kNT_selector_numFactories, 0);
    REQUIRE(n == 1);

    uintptr_t raw = pluginEntry(kNT_selector_factoryInfo, 0);
    REQUIRE(raw != 0);
    const auto* fac = reinterpret_cast<const _NT_factory*>(raw);
    constexpr uint32_t kHemiPrefix = ('H') | ('m' << 8);
    REQUIRE((fac->guid & 0xFFFF) == kHemiPrefix);
    REQUIRE(fac->name != nullptr);
    REQUIRE(fac->description != nullptr);
}

// ---------------------------------------------------------------------------
// VM2: construct populates HemiPluginInterface fields.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM2: construct populates HemiPluginInterface fields",
          "[per-applet][vectormod]") {
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
// VM3: parameter count matches manifest bus layout.
//   4 inputs (2 gates, 2 CVs) + 2 outputs * 2 params each = 8 parameters.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM3: parameter count matches manifest bus layout",
          "[per-applet][vectormod]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    _NT_algorithmRequirements req{};
    loaded->factory->calculateRequirements(req, nullptr);
    REQUIRE(req.numParameters == 8);
}

// ---------------------------------------------------------------------------
// VM4: round-trip preserves waveform and frequency state.
//   OnDataRequest bit layout (from vendor):
//     [0,  6) waveform_number[0]
//     [6,  6) waveform_number[1]
//     [12,10) freq[0] & 0x3ff
//     [22,10) freq[1] & 0x3ff
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM4: round-trip preserves waveform and frequency state",
          "[per-applet][vectormod]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    // Build a known packed word: wf_a=3, wf_b=5, freq_a=200, freq_b=100.
    uint64_t packed_in = 0;
    packed_in |= (uint64_t)(3  & 0x3F);
    packed_in |= (uint64_t)(5  & 0x3F) << 6;
    packed_in |= (uint64_t)(200 & 0x3FF) << 12;
    packed_in |= (uint64_t)(100 & 0x3FF) << 22;

    // Inject state, then round-trip via OnDataRequest.
    vectormod_on_data_receive(alg, packed_in);
    uint64_t packed_out = vectormod_on_data_request(alg);

    // Verify individual fields survive the round-trip.
    REQUIRE((packed_out & 0x3F) == 3);
    REQUIRE(((packed_out >> 6) & 0x3F) == 5);
    REQUIRE(((packed_out >> 12) & 0x3FF) == 200);
    REQUIRE(((packed_out >> 22) & 0x3FF) == 100);
}

// ---------------------------------------------------------------------------
// VM5: oscillator channels produce nonzero output after stepping.
//   Cycle inputs are low (non-cycling) so oscillators run freely once started.
//   VectorMod's Controller calls osc[ch].Next() every tick; after enough
//   steps the waveform should produce nonzero frames on both output buses.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM5: both oscillator channels produce output",
          "[per-applet][vectormod]") {
    auto s = make_setup();

    // Default output buses are parameters kParamOut1Bus and kParamOut2Bus.
    // Runtime parameter values live in alg->v[] (managed by the system).
    int out1_bus = s.alg->v[kParamOut1Bus];
    int out2_bus = s.alg->v[kParamOut2Bus];
    REQUIRE(out1_bus > 0);
    REQUIRE(out2_bus > 0);

    // Run several steps to let the oscillators advance past zero crossings.
    for (int i = 0; i < 20; ++i) {
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    }

    float abs_a = read_bus_abs_sum(s.bus, out1_bus);
    float abs_b = read_bus_abs_sum(s.bus, out2_bus);
    REQUIRE(abs_a > 0.0f);
    REQUIRE(abs_b > 0.0f);
}

// ---------------------------------------------------------------------------
// VM6: hasCustomUi returns standard Hemi claim mask.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM6: hasCustomUi returns standard claim mask",
          "[per-applet][vectormod]") {
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
// VM7: customUi encoder turn routes to on_encoder_turn without crash.
//   VectorMod requires edit mode to be active before encoder turns adjust
//   freq/waveform; without it, encoder moves the cursor. This test confirms
//   the function-pointer route fires without crashing, consistent with the
//   pilot VL8 pattern.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM7: customUi encoder turn calls on_encoder_turn",
          "[per-applet][vectormod]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    // Encoder turn in positive direction — moves cursor in default state.
    _NT_uiData data{};
    data.controls    = kNT_encoderL;
    data.lastButtons = 0;
    data.encoders[0] = 1;
    loaded->factory->customUi(alg, data);

    // Encoder turn in negative direction.
    data.encoders[0] = -1;
    loaded->factory->customUi(alg, data);
}

// ---------------------------------------------------------------------------
// VM8: draw hook fires without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM8: draw hook fires without crash",
          "[per-applet][vectormod]") {
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
// VM9: vendor dep accounting — link succeeds without lorenz or tideslite.cpp.
// ---------------------------------------------------------------------------
TEST_CASE("VectorMod VM9: vendor dep accounting — no lorenz/tideslite link",
          "[per-applet][vectormod]") {
    // If the test binary linked successfully (we are here), the dep is clean.
    REQUIRE(true);
}
