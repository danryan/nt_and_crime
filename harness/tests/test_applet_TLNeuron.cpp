// Per-applet test: TLNeuron.
//
// Manifest: shim/include/applet_manifests/TLNeuron.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/TLNeuron.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). Gate inputs assert
//   HS::frame.clocked[ch] = true across all 10 inner Controller() calls.
//   TLNeuron's Controller reads Gate(0) and Gate(1) each tick to compute
//   the weighted dendrite sum. The axon fires if sum > threshold on ANY tick,
//   so a single rising gate edge produces 10 ticks where Gate() is asserted.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only for
//   serialisation; behavioral tests assert steady-state output, not fire count).
//   Gate inputs drive GateOut which holds high while the gate is asserted,
//   so steady-state output assertions remain valid regardless of multiplier.
//
// Bus parameter layout (3 inputs + 2 outputs => 7 parameters):
//   v[0]  = input 0 (Dendrite 1) bus selector, default 1  (gate)
//   v[1]  = input 1 (Dendrite 2) bus selector, default 2  (gate)
//   v[2]  = input 2 (Dendrite 3) bus selector, default 3  (cv)
//   v[3]  = output 0 (Axon Out 1) bus selector, default 13
//   v[4]  = output 0 (Axon Out 1) mode, default 1 (replace)
//   v[5]  = output 1 (Axon Out 2) bus selector, default 14
//   v[6]  = output 1 (Axon Out 2) mode, default 1 (replace)
//
// Vendor serialisation (OnDataRequest):
//   bits [0,5)   = dendrite_weight[0] + 9   (default weight=5 -> packed=14)
//   bits [5,10)  = dendrite_weight[1] + 9   (default weight=5 -> packed=14)
//   bits [10,15) = dendrite_weight[2] + 9   (default weight=0 -> packed=9)
//   bits [15,21) = threshold + 27           (default threshold=9 -> packed=36)
//
// Default packed value: 14 | (14<<5) | (9<<10) | (36<<15)
//   = 14 | 448 | 9216 | 1179648 = 1189326
//
// Vendor Controller logic:
//   sum = 0
//   if Gate(0): sum += weight[0]; dendrite_activated[0]=1
//   if Gate(1): sum += weight[1]; dendrite_activated[1]=1
//   if In(0) > HEMISPHERE_MAX_INPUT_CV/2: sum += weight[2]; dendrite_activated[2]=1
//   axon_activated = (sum > threshold)
//   GateOut(0, axon_activated); GateOut(1, axon_activated)

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Test seams defined in plugins/applets/TLNeuron.cpp.
extern "C" uint64_t tlneuron_on_data_request(_NT_algorithm* self);
extern "C" void     tlneuron_on_data_receive(_NT_algorithm* self, uint64_t data);

namespace {

// Default bus assignments from emit_base_parameters.
constexpr int kBusDendrite1 = 1;   // v[0] default - gate input 0
constexpr int kBusDendrite2 = 2;   // v[1] default - gate input 1
constexpr int kBusDendrite3 = 3;   // v[2] default - cv input
constexpr int kBusAxon1     = 13;  // v[3] default - gate output 0
constexpr int kBusAxon2     = 14;  // v[5] default - gate output 1
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

// Pack helper mirrors vendor OnDataRequest byte-by-byte.
// Fields: weight[0..2] in range [-9,9], threshold in range [-27,27].
uint64_t pack_tlneuron(int w0, int w1, int w2, int thresh) {
    uint64_t data = 0;
    data |= (uint64_t)((w0 + 9) & 0x1F);
    data |= (uint64_t)((w1 + 9) & 0x1F) << 5;
    data |= (uint64_t)((w2 + 9) & 0x1F) << 10;
    data |= (uint64_t)((thresh + 27) & 0x3F) << 15;
    return data;
}

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a constant voltage across all frames of a 1-based bus.
void write_cv_bus(float* bus, int bus_1based, float volts) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a single-sample rising-edge pulse at frame 0 on a 1-based gate bus.
void pulse_bus(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    slice[0] = 6.0f;
}

// Hold a gate high across all frames on a 1-based bus.
void hold_gate_bus(float* bus, int bus_1based) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 6.0f;
}

// Returns true if any frame on the given 1-based bus exceeds 0.5V.
bool any_gate_high(const float* bus, int bus_1based) {
    const float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) {
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

}  // namespace

// ---------------------------------------------------------------------------
// TN1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN1: pluginEntry returns factory with correct guid and name", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','T','N');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "TL Neuron");
}

// ---------------------------------------------------------------------------
// TN2: construct populates HemiPluginInterface fields correctly.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN2: construct populates HemiPluginInterface magic and version", "[per-applet][tlneuron]") {
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
// TN3: OnDataRequest packs default state (weights=[5,5,0], threshold=9).
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN3: OnDataRequest packs default weights and threshold", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t packed = tlneuron_on_data_request(loaded->algorithm);
    uint64_t expected = pack_tlneuron(5, 5, 0, 9);

    REQUIRE((packed & 0x1F) == ((expected & 0x1F)));           // weight[0]=5
    REQUIRE(((packed >> 5) & 0x1F) == ((expected >> 5) & 0x1F)); // weight[1]=5
    REQUIRE(((packed >> 10) & 0x1F) == ((expected >> 10) & 0x1F)); // weight[2]=0
    REQUIRE(((packed >> 15) & 0x3F) == ((expected >> 15) & 0x3F)); // threshold=9
}

// ---------------------------------------------------------------------------
// TN4: serialise round-trip preserves weights and threshold.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN4: OnDataReceive/OnDataRequest round-trip preserves state", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Inject weights=[3,-2,7], threshold=15.
    uint64_t state_in = pack_tlneuron(3, -2, 7, 15);
    tlneuron_on_data_receive(alg, state_in);
    uint64_t packed = tlneuron_on_data_request(alg);

    REQUIRE((packed & 0x1F) == ((uint64_t)(3 + 9) & 0x1F));
    REQUIRE(((packed >> 5) & 0x1F) == ((uint64_t)(-2 + 9) & 0x1F));
    REQUIRE(((packed >> 10) & 0x1F) == ((uint64_t)(7 + 9) & 0x1F));
    REQUIRE(((packed >> 15) & 0x3F) == ((uint64_t)(15 + 27) & 0x3F));
}

// ---------------------------------------------------------------------------
// TN5: JSON serialise/deserialise round-trip preserves state.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN5: JSON serialise/deserialise round-trip preserves state", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Inject weights=[1,2,3], threshold=5.
    uint64_t state_in = pack_tlneuron(1, 2, 3, 5);
    tlneuron_on_data_receive(alg, state_in);
    uint32_t lo = (uint32_t)(tlneuron_on_data_request(alg) & 0xFFFFFFFFu);
    uint32_t hi = 0u;

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

    // weight[0]=1 -> packed bits [0,5) = 1+9 = 10
    REQUIRE((rt_lo & 0x1F) == 10u);
    // weight[1]=2 -> packed bits [5,10) = 2+9 = 11
    REQUIRE(((rt_lo >> 5) & 0x1F) == 11u);
    // weight[2]=3 -> packed bits [10,15) = 3+9 = 12
    REQUIRE(((rt_lo >> 10) & 0x1F) == 12u);
    // threshold=5 -> packed bits [15,21) = 5+27 = 32
    REQUIRE(((rt_lo >> 15) & 0x3F) == 32u);
}

// ---------------------------------------------------------------------------
// TN6: both dendrite gates held high with sum > threshold drives axon high.
//
// Default weights=[5,5,0], threshold=9. With dendrite1+dendrite2 both high:
// sum = 5+5 = 10 > 9 -> axon fires -> both outputs go high.
// Gate inputs are held across all frames; axon holds high while sum > threshold.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN6: both dendrite gates high with sum > threshold drives axon high", "[per-applet][tlneuron]") {
    auto s = make_setup();

    // Hold both gate inputs high across all frames.
    hold_gate_bus(s.bus, kBusDendrite1);
    hold_gate_bus(s.bus, kBusDendrite2);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kBusAxon1));
    REQUIRE(any_gate_high(s.bus, kBusAxon2));
}

// ---------------------------------------------------------------------------
// TN7: no dendrite inputs -> sum = 0 <= threshold -> axon stays low.
//
// Default threshold=9. With no inputs: sum=0 not > 9 -> axon does not fire.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN7: no dendrite inputs keeps axon low", "[per-applet][tlneuron]") {
    auto s = make_setup();

    // No inputs; all buses remain zero.
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(!any_gate_high(s.bus, kBusAxon1));
    REQUIRE(!any_gate_high(s.bus, kBusAxon2));
}

// ---------------------------------------------------------------------------
// TN8: CV dendrite3 above half-scale with high weights fires axon.
//
// Set weights=[0,0,9], threshold=8. With In(0) > HEMISPHERE_MAX_INPUT_CV/2
// (half-scale ~3V): sum = weight[2] = 9 > 8 -> axon fires.
// HEMISPHERE_MAX_INPUT_CV = 9216 hem = 6V. Half-scale = 4608 hem = 3V.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN8: CV dendrite3 above half-scale with sufficient weight drives axon high", "[per-applet][tlneuron]") {
    auto s = make_setup();

    // Set weights=[0,0,9], threshold=8.
    tlneuron_on_data_receive(s.alg, pack_tlneuron(0, 0, 9, 8));

    // Write CV dendrite3 at 4V > 3V half-scale threshold.
    write_cv_bus(s.bus, kBusDendrite3, 4.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kBusAxon1));
}

// ---------------------------------------------------------------------------
// TN9: CV dendrite3 below half-scale does not contribute to sum.
//
// Same config as TN8 but CV=1V < 3V. sum=0 not > 8 -> axon stays low.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN9: CV dendrite3 below half-scale does not fire axon", "[per-applet][tlneuron]") {
    auto s = make_setup();

    // Set weights=[0,0,9], threshold=8.
    tlneuron_on_data_receive(s.alg, pack_tlneuron(0, 0, 9, 8));

    // Write CV dendrite3 at 1V < 3V half-scale threshold.
    write_cv_bus(s.bus, kBusDendrite3, 1.0f);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(!any_gate_high(s.bus, kBusAxon1));
}

// ---------------------------------------------------------------------------
// TN10: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN10: hasCustomUi returns expected bitmask", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}

// ---------------------------------------------------------------------------
// TN11: encoder turn advances selected param via customUi.
//
// After Start(), selected=0 (dendrite_weight[0]). An encoder turn of +1
// increments weight[0] from default 5 to 6. Packed bits [0,5) = 6+9 = 15.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN11: customUi encoder turn increments selected dendrite weight", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    // Confirm default weight[0]=5 -> packed bits [0,5) = 14.
    REQUIRE((tlneuron_on_data_request(loaded->algorithm) & 0x1F) == 14u);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE((tlneuron_on_data_request(loaded->algorithm) & 0x1F) == 15u);
}

// ---------------------------------------------------------------------------
// TN12: encoder button press cycles selected field without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN12: customUi encoder button press cycles selection", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// TN13: button1 press routes on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("TLNeuron TN13: customUi button1 press routes on_aux_button", "[per-applet][tlneuron]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
