// Per-applet pilot test: Combin8.
//
// Manifest: shim/include/applet_manifests/Combin8.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Combin8.h
//
// Combin8 is a 3-input CV combiner: each output channel sums its primary
// CV input (In(ch)) with two auxiliary CVInputMap sources. The Controller()
// is purely combinatorial (no clock-driven state, no tick accumulation), so
// the 10x inner-tick multiplier does not affect assertions here.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "CV 1"   (default 1)
//   v[1]  = input  bus for "CV 2"   (default 2)
//   v[2]  = output bus for "Out 1"  (default 13)
//   v[3]  = output mode for "Out 1" (default 1 = replace)
//   v[4]  = output bus for "Out 2"  (default 14)
//   v[5]  = output mode for "Out 2" (default 1 = replace)
//
// OnDataRequest layout (PackPackables of four CVInputMap, each 16 bits):
//   Bits [0..15]  = sources[0][0].Pack()  (ch0 aux1)
//   Bits [16..31] = sources[0][1].Pack()  (ch0 aux2)
//   Bits [32..47] = sources[1][0].Pack()  (ch1 aux1)
//   Bits [48..63] = sources[1][1].Pack()  (ch1 aux2)
//
//   CVInputMap default: source=0, attenuversion=60 (0x3C).
//   Default Pack() = (0 & 0xFF) | (60 << 8) = 0x3C00.
//   Full default packed = 0x3C003C003C003C00.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

using Catch::Approx;

// Test seams defined in plugins/applets/Combin8.cpp.
uint64_t combin8_applet_on_data_request(_NT_algorithm* self);
void     combin8_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for Combin8's default parameter layout.
static constexpr int kBusCV1  = 1;   // v[0] default
static constexpr int kBusCV2  = 2;   // v[1] default
static constexpr int kBusOut1 = 13;  // v[2] default
static constexpr int kBusOut2 = 14;  // v[4] default

// Frames per step call matching the host sim default (numFramesBy4=8).
static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Default packed state: all four CVInputMap at source=0, attenuversion=60.
static constexpr uint64_t kDefaultPacked = 0x3C003C003C003C00ull;

// Write a constant CV value (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Read the last frame of a CV output bus (1-based).
static float read_cv_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Zero all frames for the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusCV1, kBusCV2, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("Combin8 CB1: OnDataRequest returns default state after Start", "[per-applet-pilot][combin8]") {
    // All four CVInputMap default to source=0 (none), attenuversion=60.
    // PackPackables produces 0x3C003C003C003C00.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = combin8_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == kDefaultPacked);
}

TEST_CASE("Combin8 CB2: serialise round-trip preserves state", "[per-applet-pilot][combin8]") {
    // Inject non-default data and confirm it round-trips cleanly.
    // sources[0][0].source=1, attenuversion=60 -> Pack() = 0x3C01.
    // Other sources unchanged. Packed = 0x3C003C003C003C01.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = 0x3C003C003C003C01ull;
    combin8_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = combin8_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == state_in);
}

TEST_CASE("Combin8 CB3: primary CV input passes through to output when aux sources are none", "[per-applet-pilot][combin8]") {
    // With aux sources at source=0 (none, contributes 0), Out(ch) == In(ch).
    // CV 1 = 2V, CV 2 = 3V. Out1 ~= 2V, Out2 ~= 3V.
    // Tolerance: per_applet_runtime converts at HEMISPHERE_MAX_CV / 5V scale.
    // The shim maps 1V -> HEMISPHERE_MAX_CV/5 = 9216/5 = 1843 hem units.
    // We allow 5% tolerance on the round-trip through float bus conversion.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 2.0f);
    write_cv_bus(bus, kBusCV2, 3.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_cv_bus(bus, kBusOut1);
    float out2 = read_cv_bus(bus, kBusOut2);
    REQUIRE(out1 == Approx(2.0f).epsilon(0.05f));
    REQUIRE(out2 == Approx(3.0f).epsilon(0.05f));
}

TEST_CASE("Combin8 CB4: zero CV input produces zero output", "[per-applet-pilot][combin8]") {
    // With all inputs at 0V and aux sources=none, both outputs are 0V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 0.0f);
    write_cv_bus(bus, kBusCV2, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_cv_bus(bus, kBusOut1) == Approx(0.0f).margin(0.01f));
    REQUIRE(read_cv_bus(bus, kBusOut2) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Combin8 CB5: encoder turn advances cursor via customUi", "[per-applet-pilot][combin8]") {
    // OnEncoderMove(1) in non-edit mode advances cursor from 0 to 1.
    // The round-trip serialisation is unchanged since only cursor moved.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // State unchanged by cursor move alone.
    uint64_t packed = combin8_applet_on_data_request(loaded->algorithm);
    REQUIRE(packed == kDefaultPacked);
}

TEST_CASE("Combin8 CB6: encoder button press routes OnButtonPress via customUi", "[per-applet-pilot][combin8]") {
    // OnButtonPress toggles edit mode or advances the input map editor.
    // Must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Combin8 CB7: button1 press routes on_aux_button via customUi", "[per-applet-pilot][combin8]") {
    // on_aux_button maps to OnButtonPress. Must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Combin8 CB8: two independent channels produce independent outputs", "[per-applet-pilot][combin8]") {
    // CV1=1V and CV2=4V with no aux sources: Out1 ~= 1V, Out2 ~= 4V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusCV1, 1.0f);
    write_cv_bus(bus, kBusCV2, 4.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out1 = read_cv_bus(bus, kBusOut1);
    float out2 = read_cv_bus(bus, kBusOut2);
    REQUIRE(out1 == Approx(1.0f).epsilon(0.05f));
    REQUIRE(out2 == Approx(4.0f).epsilon(0.05f));
    REQUIRE(out2 > out1 + 2.0f);
}
