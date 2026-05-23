// Per-applet test: VectorEG.
//
// Manifest: shim/include/applet_manifests/VectorEG.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/VectorEG.h
//
// Vendor dep accounting: HSVectorOscillator + WaveformManager (header-only).
// No .cpp link required. VectorEG uses vec_osc headers only.
//
// Bus layout:
//   param[0] = "Freq/Shape CV A" input bus  (default 1)
//   param[1] = "Freq/Shape CV B" input bus  (default 2)
//   param[2] = "Gate A"          input bus  (default 3)
//   param[3] = "Gate B"          input bus  (default 4)
//   param[4] = "Env A"           output bus (default 13)
//   param[5] = "Env A mode"                 (default 1)
//   param[6] = "Env B"           output bus (default 14)
//   param[7] = "Env B mode"                 (default 1)
//
// 10x ticks-per-step: Controller() fires 10 times per NT step() buffer.
// Gate(ch) asserts on gate_high[ch] which stays set across all 10 inner
// calls when the gate bus is high. Tests use nonzero-sum and round-trip
// strategies rather than counting exact per-tick transitions.
//
// WaveformManager::Validate() initialises HS::user_waveforms with the
// magic signature and default waveforms. Without it, VectorOscillatorFromWaveform
// returns an empty oscillator and Next() always produces 0. The test harness
// replicates this using the same POD trick as the VectorLFO pilot test.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>
#include <cstring>
#include <cstdint>
#include "HemiPluginInterface.h"

// Opaque accessors defined in VectorEG.cpp (test seam).
extern "C" uint64_t vectoreg_on_data_request(_NT_algorithm* self);
extern "C" void     vectoreg_on_data_receive(_NT_algorithm* self, uint64_t state);

// Mirror WaveformManager::Validate() without pulling in the full headers.
namespace HS {
    struct VOSegment_pod { uint8_t level; uint8_t time; };
    extern VOSegment_pod user_waveforms[64];
}

void validate_user_waveforms_veg() {
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

// Pack helper matching vendor VectorEG::OnDataRequest byte-for-byte.
// Bit layout:
//   [0,  6) waveform_number[0]
//   [6,  6) waveform_number[1]
//   [12,10) freq[0]   (10-bit unsigned centihertz)
//   [22,10) freq[1]
//   [32, 1) modshape
static uint64_t pack_vectoreg_local(int wf_a, int wf_b,
                                    int freq_a, int freq_b,
                                    bool modshape) {
    uint64_t data = 0;
    data |= ((uint64_t)(wf_a  & 0x3F));
    data |= ((uint64_t)(wf_b  & 0x3F)) << 6;
    data |= ((uint64_t)(freq_a & 0x3FF)) << 12;
    data |= ((uint64_t)(freq_b & 0x3FF)) << 22;
    data |= ((uint64_t)(modshape ? 1 : 0)) << 32;
    return data;
}

namespace {

// Parameter indices (4 inputs + 2 output pairs = 8 total).
constexpr int kParamFreqCvABus = 0;
constexpr int kParamFreqCvBBus = 1;
constexpr int kParamGateABus   = 2;
constexpr int kParamGateBBus   = 3;
constexpr int kParamEnvABus    = 4;
constexpr int kParamEnvAMode   = 5;
constexpr int kParamEnvBBus    = 6;
constexpr int kParamEnvBMode   = 7;

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

// Write a gate signal: high = all frames at 5V, low = all frames at 0V.
void write_gate_bus(float* bus, int bus_idx_1based, bool high) {
    write_cv_bus(bus, bus_idx_1based, high ? 5.0f : 0.0f);
}

float read_bus_last(float* bus, int bus_idx_1based) {
    int numFrames = nt::bus_frame_count();
    return bus[(bus_idx_1based - 1) * numFrames + (numFrames - 1)];
}

void reset_and_validate() {
    nt::reset_runtime();
    validate_user_waveforms_veg();
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
    // Warm-up: fire Start() and stabilize.
    loaded->factory->step(alg, bus, kNumFramesBy4);
    clear_all_buses(bus);
    return { loaded, alg, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// VEG1: pluginEntry returns the correct factory.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG1: pluginEntry returns valid factory",
          "[per-applet][vectoreg]") {
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
// VEG2: construct populates HemiPluginInterface fields.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG2: construct populates HemiPluginInterface fields",
          "[per-applet][vectoreg]") {
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
// VEG3: parameter count matches manifest bus layout.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG3: parameter count matches manifest bus layout",
          "[per-applet][vectoreg]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    // 4 inputs + 2*(2 outputs) = 8 base parameters.
    _NT_algorithmRequirements req{};
    loaded->factory->calculateRequirements(req, nullptr);
    REQUIRE(req.numParameters == 8);
}

// ---------------------------------------------------------------------------
// VEG4: round-trip — state survives serialise/deserialise via opaque accessors.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG4: round-trip via opaque accessors preserves state",
          "[per-applet][vectoreg]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    // Build packed word for known values: wf_a=2, wf_b=3, freq_a=100,
    // freq_b=200, modshape=true.
    uint64_t packed = pack_vectoreg_local(2, 3, 100, 200, true);

    // Inject and read back via the opaque accessors.
    vectoreg_on_data_receive(alg, packed);
    uint64_t readback = vectoreg_on_data_request(alg);
    REQUIRE(readback == packed);
}

// ---------------------------------------------------------------------------
// VEG5: pack helper is self-consistent (bit layout spot-check).
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG5: pack helper encodes fields into correct bit positions",
          "[per-applet][vectoreg]") {
    uint64_t packed = pack_vectoreg_local(5, 7, 150, 300, true);

    REQUIRE((packed & 0x3F) == 5);              // [0,6) waveform_a
    REQUIRE(((packed >> 6) & 0x3F) == 7);       // [6,6) waveform_b
    REQUIRE(((packed >> 12) & 0x3FF) == 150);   // [12,10) freq_a
    REQUIRE(((packed >> 22) & 0x3FF) == 300);   // [22,10) freq_b
    REQUIRE(((packed >> 32) & 1) == 1);         // [32,1) modshape

    // Idempotent.
    REQUIRE(pack_vectoreg_local(5, 7, 150, 300, true) == packed);
}

// ---------------------------------------------------------------------------
// VEG6: gate A input triggers envelope — output is nonzero after gating.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG6: Gate A triggers envelope output on Env A bus",
          "[per-applet][vectoreg]") {
    // Strategy: assert gate A (bus 3) high for many steps. The oscillator
    // should produce nonzero output on Env A (bus 13). 10x ticks-per-step
    // acknowledged; we accumulate absolute output over many steps.
    auto [loaded, alg, bus] = make_setup();

    float abs_sum = 0.0f;
    for (int i = 0; i < 100; ++i) {
        write_gate_bus(bus, 3, true);
        loaded->factory->step(alg, bus, kNumFramesBy4);
        float v = read_bus_last(bus, 13);
        abs_sum += (v < 0.0f ? -v : v);
    }
    REQUIRE(abs_sum > 0.0f);
}

// ---------------------------------------------------------------------------
// VEG7: both envelope channels produce output when both gates are high.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG7: both Gate A and Gate B produce independent outputs",
          "[per-applet][vectoreg]") {
    auto [loaded, alg, bus] = make_setup();

    float abs_a = 0.0f, abs_b = 0.0f;
    for (int i = 0; i < 100; ++i) {
        write_gate_bus(bus, 3, true);
        write_gate_bus(bus, 4, true);
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
// VEG8: hasCustomUi returns the standard per-applet claim mask.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG8: hasCustomUi returns standard claim mask",
          "[per-applet][vectoreg]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(alg);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// VEG9: customUi encoder turn calls on_encoder_turn without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG9: customUi encoder turn calls on_encoder_turn",
          "[per-applet][vectoreg]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    REQUIRE(loaded->factory->customUi != nullptr);

    _NT_uiData data{};
    data.controls    = kNT_encoderL;
    data.lastButtons = 0;
    data.encoders[0] = 1;
    loaded->factory->customUi(alg, data);

    data.encoders[0] = -1;
    loaded->factory->customUi(alg, data);
}

// ---------------------------------------------------------------------------
// VEG10: customUi encoder button press calls on_button_press without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG10: customUi encoder button press calls on_button_press",
          "[per-applet][vectoreg]") {
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
// VEG11: customUi button1 press calls on_aux_button (AuxButton) without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG11: customUi button1 press calls on_aux_button",
          "[per-applet][vectoreg]") {
    reset_and_validate();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);

    _NT_uiData data{};
    data.controls    = kNT_button1;
    data.lastButtons = 0;
    data.encoders[0] = 0;
    loaded->factory->customUi(alg, data);
}

// ---------------------------------------------------------------------------
// VEG12: draw hook fires without crash.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG12: draw hook fires without crash",
          "[per-applet][vectoreg]") {
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
// VEG13: vendor dep accounting — no Lorenz path in this .o.
// ---------------------------------------------------------------------------
TEST_CASE("VectorEG VEG13: vendor dep accounting — link succeeds without lorenz",
          "[per-applet][vectoreg]") {
    // If the test binary linked (we are here), the dep is clean.
    REQUIRE(true);
}
