// Per-applet test: Binary.
//
// Manifest: shim/include/applet_manifests/Binary.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Binary.h
//
// Binary is a 4-bit binary counter. Each input channel thresholds at either
// the gate level (>0.5V for Gate A/B) or 3V (for CV A/B). The four bits
// drive two outputs: a weighted binary sum (Output A) and a popcount (Output B).
//
// 10x-multiplier note: Binary's Controller() is purely combinatorial.  No
// counter or toggle advances inside the inner-tick loop; all 10 ticks per
// buffer produce the same output given constant inputs. No special modelling
// is required.
//
// Bus parameter layout (emit_base_parameters with 4 inputs, 2 outputs):
//   v[0]  = input bus for "Gate A"  (default 1, gate kind)
//   v[1]  = input bus for "Gate B"  (default 2, gate kind)
//   v[2]  = input bus for "CV A"    (default 3, cv kind)
//   v[3]  = input bus for "CV B"    (default 4, cv kind)
//   v[4]  = output bus for "Binary" (default 13)
//   v[5]  = output mode for "Binary"(default 1 = replace)
//   v[6]  = output bus for "Count"  (default 14)
//   v[7]  = output mode for "Count" (default 1 = replace)
//
// Constants (from vendor Binary.h and shim HSUtils.h):
//   HEMISPHERE_MAX_CV = 9216 hem
//   B0Val = 9216 / 15 = 614 hem  (weight of bit 0)
//   CVal  = 9216 / 4  = 2304 hem (weight per count unit = 1V per bit)
//   HEMISPHERE_3V_CV  = 4608 hem (CV threshold: bit is 1 if In(ch) > 4608)
//   1 hem unit = 1/1536 V  (ONE_OCTAVE = 1536)
//
// step_impl fixup: Binary reads In(0)=inputs[0] and In(1)=inputs[1], but
// populate_frame_from_bus writes CV values to inputs[2] and inputs[3]
// (manifest positions 2,3). Binary.cpp copies inputs[2,3] to inputs[0,1]
// after populate so In(0)/In(1) resolve correctly.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

using Catch::Approx;

// Test seams defined in plugins/applets/Binary.cpp.
uint64_t binary_on_data_request(_NT_algorithm* self);
void     binary_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices from the manifest (4 inputs, 2 outputs).
static constexpr int kBusGateA  = 1;   // v[0] default
static constexpr int kBusGateB  = 2;   // v[1] default
static constexpr int kBusCVA    = 3;   // v[2] default
static constexpr int kBusCVB    = 4;   // v[3] default
static constexpr int kBusBinary = 13;  // v[4] default (Output A)
static constexpr int kBusCount  = 14;  // v[6] default (Output B)

static constexpr int kNumFrames    = 32;
static constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

// Fill all frames of a 1-based bus with a constant voltage.
static void write_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Read the last-frame value of a 1-based output bus in volts.
static float read_bus_last(const float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Zero all frames for buses used by Binary.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusGateA, kBusGateB, kBusCVA, kBusCVB, kBusBinary, kBusCount}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("Binary BN1: OnDataRequest returns 0 (no serialisable state)", "[per-applet][binary]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = binary_on_data_request(loaded->algorithm);
    REQUIRE(packed == 0u);
}

TEST_CASE("Binary BN2: all inputs low produces zero outputs", "[per-applet][binary]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    // All inputs at 0V: gate_high = false, In(ch) = 0 < threshold.
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_bus_last(bus, kBusBinary) == 0.0f);
    REQUIRE(read_bus_last(bus, kBusCount)  == 0.0f);
}

TEST_CASE("Binary BN3: Gate A high sets bit 0 (weight 8*B0Val)", "[per-applet][binary]") {
    // Gate A = 5V -> gate_high[0] = true -> bit[0] = 1.
    // sum = bit[0] * 8 * B0Val = 1 * 8 * 614 = 4912 hem.
    // Output A = 4912 / 1536 = ~3.198V.
    // count = 1 * CVal = 2304 hem = 1.5V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus(bus, kBusGateA, 5.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out_binary = read_bus_last(bus, kBusBinary);
    float out_count  = read_bus_last(bus, kBusCount);
    // 4912 hem / 1536 = 3.1979... V
    REQUIRE(out_binary == Approx(4912.0f / 1536.0f).epsilon(0.01f));
    // 2304 hem / 1536 = 1.5V
    REQUIRE(out_count  == Approx(1.5f).epsilon(0.01f));
}

TEST_CASE("Binary BN4: Gate B high sets bit 1 (weight 4*B0Val)", "[per-applet][binary]") {
    // Gate B = 5V -> gate_high[1] = true -> bit[1] = 1.
    // sum = bit[1] * 4 * B0Val = 1 * 4 * 614 = 2456 hem = ~1.599V.
    // count = 1 * CVal = 2304 hem = 1.5V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus(bus, kBusGateB, 5.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out_binary = read_bus_last(bus, kBusBinary);
    float out_count  = read_bus_last(bus, kBusCount);
    REQUIRE(out_binary == Approx(2456.0f / 1536.0f).epsilon(0.01f));
    REQUIRE(out_count  == Approx(1.5f).epsilon(0.01f));
}

TEST_CASE("Binary BN5: CV A above 3V threshold sets bit 2 (weight 2*B0Val)", "[per-applet][binary]") {
    // CV A = 4V -> In(0) = int(4.0 * 1536) = 6144 hem > 4608 (3V threshold).
    // bit[2] = 1. sum = 2 * 614 = 1228 hem = ~0.799V.
    // count = 1 * CVal = 2304 hem = 1.5V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus(bus, kBusCVA, 4.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out_binary = read_bus_last(bus, kBusBinary);
    float out_count  = read_bus_last(bus, kBusCount);
    REQUIRE(out_binary == Approx(1228.0f / 1536.0f).epsilon(0.01f));
    REQUIRE(out_count  == Approx(1.5f).epsilon(0.01f));
}

TEST_CASE("Binary BN6: CV B above 3V threshold sets bit 3 (weight 1*B0Val)", "[per-applet][binary]") {
    // CV B = 4V -> In(1) = 6144 hem > 4608 -> bit[3] = 1.
    // sum = 1 * 614 = 614 hem = ~0.400V.
    // count = 1 * CVal = 2304 hem = 1.5V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus(bus, kBusCVB, 4.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out_binary = read_bus_last(bus, kBusBinary);
    float out_count  = read_bus_last(bus, kBusCount);
    REQUIRE(out_binary == Approx(614.0f / 1536.0f).epsilon(0.01f));
    REQUIRE(out_count  == Approx(1.5f).epsilon(0.01f));
}

TEST_CASE("Binary BN7: CV below 3V threshold does not set bit", "[per-applet][binary]") {
    // CV A = 2V -> In(0) = 3072 hem < 4608: bit[2] = 0. No output change.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus(bus, kBusCVA, 2.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_bus_last(bus, kBusBinary) == 0.0f);
    REQUIRE(read_bus_last(bus, kBusCount)  == 0.0f);
}

TEST_CASE("Binary BN8: all four bits high produces maximum outputs", "[per-applet][binary]") {
    // Gate A=5V, Gate B=5V -> bit[0]=1, bit[1]=1.
    // CV A=4V, CV B=4V -> bit[2]=1, bit[3]=1.
    // sum = 614 + 1228 + 2456 + 4912 = 9210 hem = ~5.996V.
    // count = 4 * 2304 = 9216 hem = 6.0V.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_bus(bus, kBusGateA, 5.0f);
    write_bus(bus, kBusGateB, 5.0f);
    write_bus(bus, kBusCVA,   4.0f);
    write_bus(bus, kBusCVB,   4.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float out_binary = read_bus_last(bus, kBusBinary);
    float out_count  = read_bus_last(bus, kBusCount);
    // sum = 614+1228+2456+4912 = 9210 hem
    REQUIRE(out_binary == Approx(9210.0f / 1536.0f).epsilon(0.01f));
    // count = 4 * 2304 = 9216 hem
    REQUIRE(out_count  == Approx(9216.0f / 1536.0f).epsilon(0.01f));
}

TEST_CASE("Binary BN9: encoder button press routes cleanly (no-op)", "[per-applet][binary]") {
    // Binary::OnButtonPress is empty. Verify routing does not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Binary BN10: aux button press routes cleanly (no-op)", "[per-applet][binary]") {
    // Binary::on_aux_button maps to OnButtonPress (empty). Must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
