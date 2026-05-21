// Per-applet pilot test: Logic.
//
// Manifest: shim/include/applet_manifests/Logic.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Logic.h
//
// No 10x ticks-per-step concern: Logic Controller() reads Gate(0) and Gate(1)
// combinatorially and writes GateOut each tick. The gate state stays asserted
// across all 10 inner ticks, so steady-state output is correct regardless of
// tick count.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Gate 1"   (default 1)
//   v[1]  = input  bus for "Gate 2"   (default 2)
//   v[2]  = output bus for "Out 1"    (default 13)
//   v[3]  = output mode for "Out 1"   (default 1 = replace)
//   v[4]  = output bus for "Out 2"    (default 14)
//   v[5]  = output mode for "Out 2"   (default 1 = replace)
//
// Vendor data layout (OnDataRequest):
//   bits [0,8)  = operation[0]  (gate 0 logic function index, default 0 = AND)
//   bits [8,16) = operation[1]  (gate 1 logic function index, default 2 = XOR)
//
// Logic functions (HEMISPHERE_NUMBER_OF_LOGIC = 7):
//   0=AND, 1=OR, 2=XOR, 3=NAND, 4=NOR, 5=XNOR, 6=CV-controlled

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Logic.cpp.
uint64_t logic_applet_on_data_request(_NT_algorithm* self);
void     logic_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices.
static constexpr int kBusGate1 = 1;   // v[0] default
static constexpr int kBusGate2 = 2;   // v[1] default
static constexpr int kBusOut1  = 13;  // v[2] default
static constexpr int kBusOut2  = 14;  // v[4] default

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a gate high (5V) or low (0V) signal across all frames of a 1-based bus.
static void write_gate_bus(float* busFrames, int bus_1based, bool high) {
    float val = high ? 5.0f : 0.0f;
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = val;
}

// Returns true if the last frame of the output gate bus exceeds 0.5V.
static bool read_gate_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)] > 0.5f;
}

// Zero the buses used by this plug-in.
static void clear_buses(float* busFrames) {
    for (int bus : {kBusGate1, kBusGate2, kBusOut1, kBusOut2}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("Logic LG1: OnDataRequest packs operation[0]=0 and operation[1]=2 after Start",
          "[per-applet][logic]") {
    // Vendor Start() sets operation[0]=0 (AND) and operation[1]=2 (XOR).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = logic_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 0u);           // operation[0] = AND
    REQUIRE(((packed >> 8) & 0xFF) == 2u);    // operation[1] = XOR
}

TEST_CASE("Logic LG2: serialise round-trip preserves both operations",
          "[per-applet][logic]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject operation[0]=3 (NAND), operation[1]=4 (NOR).
    uint64_t state_in = (4u << 8) | 3u;
    logic_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = logic_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0xFF) == 3u);
    REQUIRE(((packed >> 8) & 0xFF) == 4u);
}

TEST_CASE("Logic LG3: AND gate - both high produces Out1 high",
          "[per-applet][logic]") {
    // Default operation[0]=AND. Gate1=H, Gate2=H -> Out1=H.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate1, true);
    write_gate_bus(bus, kBusGate2, true);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusOut1) == true);
}

TEST_CASE("Logic LG4: AND gate - one input low produces Out1 low",
          "[per-applet][logic]") {
    // Default operation[0]=AND. Gate1=H, Gate2=L -> Out1=L.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate1, true);
    write_gate_bus(bus, kBusGate2, false);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusOut1) == false);
}

TEST_CASE("Logic LG5: XOR gate - both inputs equal produces Out2 low",
          "[per-applet][logic]") {
    // Default operation[1]=XOR. Gate1=H, Gate2=H -> XOR=L -> Out2=L.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate1, true);
    write_gate_bus(bus, kBusGate2, true);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusOut2) == false);
}

TEST_CASE("Logic LG6: XOR gate - differing inputs produces Out2 high",
          "[per-applet][logic]") {
    // Default operation[1]=XOR. Gate1=H, Gate2=L -> XOR=H -> Out2=H.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate1, true);
    write_gate_bus(bus, kBusGate2, false);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusOut2) == true);
}

TEST_CASE("Logic LG7: encoder turn in edit mode changes operation via customUi",
          "[per-applet][logic]") {
    // OnEncoderMove only changes operation when EditMode() is true.
    // Press the encoder button to enter edit mode, then turn to advance
    // operation[0] from 0 (AND) to 1 (OR).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE((logic_applet_on_data_request(loaded->algorithm) & 0xFF) == 0u);

    // Enter edit mode via encoder button press.
    _NT_uiData ui_press{};
    ui_press.encoders[0]  = 0;
    ui_press.controls     = kNT_encoderButtonL;
    ui_press.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui_press);

    // Now turn the encoder to advance operation[0].
    _NT_uiData ui_turn{};
    ui_turn.encoders[0]  = 1;
    ui_turn.controls     = 0;
    ui_turn.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui_turn);

    REQUIRE((logic_applet_on_data_request(loaded->algorithm) & 0xFF) == 1u);
}

TEST_CASE("Logic LG8: encoder button press routes without crash",
          "[per-applet][logic]") {
    // Logic has no OnButtonPress override; confirm routing does not crash.
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

TEST_CASE("Logic LG9: NAND gate - both high produces Out1 low after state inject",
          "[per-applet][logic]") {
    // Inject operation[0]=3 (NAND). Gate1=H, Gate2=H -> NAND=L -> Out1=L.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = (2u << 8) | 3u;  // op[0]=NAND, op[1]=XOR
    logic_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate1, true);
    write_gate_bus(bus, kBusGate2, true);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusOut1) == false);
}

TEST_CASE("Logic LG10: OR gate - one input high produces Out1 high after state inject",
          "[per-applet][logic]") {
    // Inject operation[0]=1 (OR). Gate1=L, Gate2=H -> OR=H -> Out1=H.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = (2u << 8) | 1u;  // op[0]=OR, op[1]=XOR
    logic_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_gate_bus(bus, kBusGate1, false);
    write_gate_bus(bus, kBusGate2, true);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    REQUIRE(read_gate_bus(bus, kBusOut1) == true);
}
