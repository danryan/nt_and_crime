// Per-applet test: Voltage.
//
// Manifest: shim/include/applet_manifests/Voltage.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Voltage.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer.
//   Voltage::Controller() reads Gate(ch) and writes Out(ch) every tick.
//   Gate(ch) reads gate_high[ch] (not clocked[ch]), which is a sustained
//   level flag that stays consistent across all 10 inner ticks. Output is
//   the same on every inner tick given constant gate input. No accumulation
//   or edge-driven counters exist. Coverage shape: SHAPE 1 (bus-level
//   assertions are sound because the result is identical on every inner tick).
//
// Bus parameter layout (2 gate inputs + 2 CV outputs):
//   v[0]  = Gate 1 input  bus, default 1
//   v[1]  = Gate 2 input  bus, default 2
//   v[2]  = Volt 1 output bus, default 13
//   v[3]  = Volt 1 mode,      default 1 (replace)
//   v[4]  = Volt 2 output bus, default 14
//   v[5]  = Volt 2 mode,      default 1 (replace)
//
// Vendor serialisation (OnDataRequest):
//   bits [0,9)   = voltage[0] + 256  (bias: 256; range [-72..72] + 256 = [184..328])
//   bit  [9]     = SKIPPED (gap; pack helper must zero this bit explicitly)
//   bits [10,9)  = voltage[1] + 256
//   bit  [19]    = gate[0]  (0 = normally on / gate turns off, 1 = normally off / gate turns on)
//   bit  [20]    = gate[1]
//
// After Start(): voltage[0]=72 (VOLTAGE_MAX), voltage[1]=-72 (VOLTAGE_MIN),
//   gate[0]=0, gate[1]=0.
//   packed bits[0..8]  = 72 + 256 = 328 = 0x148
//   bit[9]             = 0 (explicit gap zero)
//   packed bits[10..18] = -72 + 256 = 184 = 0x0B8
//   bit[19]            = 0 (gate[0])
//   bit[20]            = 0 (gate[1])
//
// Key behaviours:
//   gate[ch] = 0 (normally on): Out = voltage*INCREMENTS when gate LOW; 0 when gate HIGH.
//   gate[ch] = 1 (normally off): Out = voltage*INCREMENTS when gate HIGH; 0 when gate LOW.
//
// HEMISPHERE_MAX_CV = 6 * 1536 = 9216 hem; VOLTAGE_INCREMENTS = 128.
// VOLTAGE_MAX = 9216/128 = 72; VOLTAGE_MIN = -9216/128 = -72.
// Output in volts: hem_value / 1536.0f.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

#include <cstring>
#include <string>

// Test seams defined in plugins/applets/Voltage.cpp.
uint64_t voltage_applet_on_data_request(_NT_algorithm* self);
void     voltage_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

namespace {

constexpr int kBusGate1 = 1;   // v[0] default - Gate 1 input
constexpr int kBusGate2 = 2;   // v[1] default - Gate 2 input
constexpr int kBusOut1  = 13;  // v[2] default - Volt 1 output
constexpr int kBusOut2  = 14;  // v[4] default - Volt 2 output
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

// VOLTAGE constants mirrored from vendor.
constexpr int kVoltageIncrements = 128;
constexpr float kOneOctave       = 1536.0f;

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a sustained gate-high value across all frames of a 1-based bus.
void set_gate_high(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) slice[i] = 5.0f;
}

// Return the last-frame value on a 1-based bus.
float read_last(const float* bus, int bus_1based) {
    return bus[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Pack Voltage state matching vendor OnDataRequest encoding.
// voltage bias: +256.  Bit 9 is an explicit gap and must be zero.
static uint64_t pack_voltage(int voltage0, int voltage1, bool gate0, bool gate1) {
    uint64_t d = 0;
    d |= (uint64_t)((voltage0 + 256) & 0x1FF) << 0;
    // bit 9 is the gap; explicitly left zero.
    d |= (uint64_t)((voltage1 + 256) & 0x1FF) << 10;
    d |= (uint64_t)(gate0 ? 1 : 0)            << 19;
    d |= (uint64_t)(gate1 ? 1 : 0)            << 20;
    return d;
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
    return Setup{ loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// VT1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT1: factory guid and name are correct", "[per-applet][voltage]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','V','o');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Voltage");
}

// ---------------------------------------------------------------------------
// VT2: OnDataRequest after Start encodes default state correctly.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT2: OnDataRequest default state after Start", "[per-applet][voltage]") {
    // After Start: voltage[0]=72, voltage[1]=-72, gate[0]=0, gate[1]=0.
    // Expected: bit[9] gap is zero; bias 256 applied to both voltages.
    auto s = make_setup();
    uint64_t packed   = voltage_applet_on_data_request(s.alg);
    uint64_t expected = pack_voltage(72, -72, false, false);
    REQUIRE(packed == expected);
}

// ---------------------------------------------------------------------------
// VT3: Round-trip serialisation preserves all fields.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT3: round-trip serialisation preserves all fields", "[per-applet][voltage]") {
    // Inject: voltage[0]=30, voltage[1]=-15, gate[0]=true, gate[1]=false.
    // Bit 9 gap must survive the round-trip as zero.
    auto s = make_setup();
    uint64_t state_in = pack_voltage(30, -15, true, false);
    voltage_applet_on_data_receive(s.alg, state_in);
    uint64_t packed = voltage_applet_on_data_request(s.alg);
    REQUIRE(packed == state_in);
}

// ---------------------------------------------------------------------------
// VT4: Gate low with gate[0]=0 (normally on) passes voltage to Out1.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT4: normally-on mode passes voltage to Out1 when gate is low",
          "[per-applet][voltage]") {
    // gate[0]=0 (normally on): cv = voltage[0] * INCREMENTS when Gate(0) is LOW.
    // Inject voltage[0]=36 (36 * 128 = 4608 hem = 3V). Gate1 bus stays low.
    auto s = make_setup();
    uint64_t state_in = pack_voltage(36, -72, false, false);
    voltage_applet_on_data_receive(s.alg, state_in);

    // Gate1 bus is already zero (gate LOW).
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out1 = read_last(s.bus, kBusOut1);
    float expected = (float)(36 * kVoltageIncrements) / kOneOctave;  // 3.0V
    REQUIRE(out1 == Catch::Approx(expected).epsilon(0.02f));
}

// ---------------------------------------------------------------------------
// VT5: Gate high with gate[0]=0 (normally on) mutes Out1 to zero.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT5: normally-on mode mutes Out1 when gate is high",
          "[per-applet][voltage]") {
    // gate[0]=0 (normally on): cv = 0 when Gate(0) is HIGH.
    auto s = make_setup();
    // Default state: voltage[0]=72, gate[0]=0.
    set_gate_high(s.bus, kBusGate1);

    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out1 = read_last(s.bus, kBusOut1);
    REQUIRE(out1 == Catch::Approx(0.0f).epsilon(0.001f));
}

// ---------------------------------------------------------------------------
// VT6: Gate high with gate[0]=1 (normally off) passes voltage to Out1.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT6: normally-off mode passes voltage to Out1 when gate is high",
          "[per-applet][voltage]") {
    // gate[0]=1 (normally off): cv = voltage[0]*INCREMENTS when Gate(0) is HIGH.
    // Inject voltage[0]=48, gate[0]=true (normally off). Gate1 bus high.
    auto s = make_setup();
    uint64_t state_in = pack_voltage(48, -72, true, false);
    voltage_applet_on_data_receive(s.alg, state_in);

    set_gate_high(s.bus, kBusGate1);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out1 = read_last(s.bus, kBusOut1);
    float expected = (float)(48 * kVoltageIncrements) / kOneOctave;  // 4.0V
    REQUIRE(out1 == Catch::Approx(expected).epsilon(0.02f));
}

// ---------------------------------------------------------------------------
// VT7: Gate low with gate[0]=1 (normally off) mutes Out1 to zero.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT7: normally-off mode mutes Out1 when gate is low",
          "[per-applet][voltage]") {
    // gate[0]=1 (normally off): cv = 0 when Gate(0) is LOW.
    // Inject gate[0]=true, gate low on bus.
    auto s = make_setup();
    uint64_t state_in = pack_voltage(72, -72, true, false);
    voltage_applet_on_data_receive(s.alg, state_in);

    // Gate1 bus stays zero (gate LOW).
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out1 = read_last(s.bus, kBusOut1);
    REQUIRE(out1 == Catch::Approx(0.0f).epsilon(0.001f));
}

// ---------------------------------------------------------------------------
// VT8: Ch1 (Out2) behaves independently of Ch0 (Out1).
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT8: Out2 controlled independently by gate[1]",
          "[per-applet][voltage]") {
    // gate[1]=0 (normally on), Gate2 bus low -> Out2 = voltage[1]*INCREMENTS.
    // voltage[1]=-36 (-36*128 = -4608 hem = -3V).
    auto s = make_setup();
    uint64_t state_in = pack_voltage(72, -36, false, false);
    voltage_applet_on_data_receive(s.alg, state_in);

    // Gate2 bus stays low.
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float out2 = read_last(s.bus, kBusOut2);
    float expected = (float)(-36 * kVoltageIncrements) / kOneOctave;  // -3.0V
    REQUIRE(out2 == Catch::Approx(expected).epsilon(0.02f));
}

// ---------------------------------------------------------------------------
// VT9: Encoder turn without edit mode moves cursor, does not change state.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT9: encoder turn without edit mode leaves state unchanged",
          "[per-applet][voltage]") {
    auto s = make_setup();
    uint64_t before = voltage_applet_on_data_request(s.alg);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, ui);

    uint64_t after = voltage_applet_on_data_request(s.alg);
    REQUIRE(after == before);
}

// ---------------------------------------------------------------------------
// VT10: Encoder button press routes without crash.
// ---------------------------------------------------------------------------
TEST_CASE("Voltage VT10: encoder button press via customUi does not crash",
          "[per-applet][voltage]") {
    auto s = make_setup();

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    s.loaded->factory->customUi(s.alg, ui);
    REQUIRE(true);
}
