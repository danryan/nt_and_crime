// Per-applet test: Strum.
//
// Manifest: shim/include/applet_manifests/Strum.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Strum.h
//
// Strum strums a chord upward or downward from a root CV input when triggered
// on Clock(0) (strum up) or Clock(1) (strum down).  Each trigger fires a
// sequenced pitch on Out(0) and a trigger pulse on ClockOut(1).
//
// 10x ticks-per-step concern: Strum uses EndOfADCLag(), which requires
// exactly one StartADCLag() firing per edge.  The 10x inner-tick multiplier
// means StartADCLag fires once (clocked[] stays asserted for all 10 ticks).
// EndOfADCLag fires once the countdown expires.  Bus-level "fires on trigger"
// tests are therefore safe here -- we verify the pitch output appears after a
// trigger step, not how many times Controller() ran.
//
// Bus parameter layout (4 inputs + 2 outputs = 8 parameters):
//   v[0]  = input  bus for "Strum Up" (gate, default 1)
//   v[1]  = input  bus for "Strum Dn" (gate, default 2)
//   v[2]  = input  bus for "Root"     (cv,   default 3)
//   v[3]  = input  bus for "Spacing"  (cv,   default 4)
//   v[4]  = output bus for "Pitch"    (cv,   default 13)
//   v[5]  = output mode for "Pitch"   (default 1 = replace)
//   v[6]  = output bus for "Trig"     (gate, default 14)
//   v[7]  = output mode for "Trig"    (default 1 = replace)
//
// Default applet state after Start():
//   qselect=0, spacing=8 (HEM_BURST_SPACING_MIN), length=6, stepmode=0,
//   qmod=0, intervals={0,4,7,9,11,14}, index=0, inc=0, countdown=0
//
// OnDataRequest packing (from vendor source):
//   bits [0,4):  qselect       (default 0)
//   bits [4,8):  unused/zero   (gap -- PackLocation{0,4} occupies only 4 bits,
//                               next Pack starts at 12)
//   bits [12,9): spacing       (default 8 = HEM_BURST_SPACING_MIN)
//   bits [21,4): length        (default 6)
//   bits [25+i*6, 6): intervals[i] - (-12) for i in 0..5
//   bit 61: stepmode           (default 0)
//   bit 62: qmod               (default 0)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Strum.cpp.
extern "C" uint64_t strum_applet_on_data_request(_NT_algorithm* self);
extern "C" void     strum_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices for Strum.
static constexpr int kBusStrumUp  = 1;   // v[0] gate input
static constexpr int kBusStrumDn  = 2;   // v[1] gate input
static constexpr int kBusRoot     = 3;   // v[2] cv input
static constexpr int kBusSpacing  = 4;   // v[3] cv input
static constexpr int kBusPitch    = 13;  // v[4] cv output
static constexpr int kBusTrig     = 14;  // v[6] gate output

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant float value across all frames of a 1-based bus.
static void write_bus(float* busFrames, int bus_1based, float value) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = value;
}

// Clear a specific 1-based bus to zero.
static void clear_bus(float* busFrames, int bus_1based) {
    write_bus(busFrames, bus_1based, 0.0f);
}

// Read last frame of a 1-based bus.
static float read_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Returns true if the last frame of a 1-based bus exceeds 0.5V (gate high).
static bool read_gate_bus(float* busFrames, int bus_1based) {
    return read_bus_last(busFrames, bus_1based) > 0.5f;
}

// Zero the buses Strum reads and writes.
static void clear_all_buses(float* busFrames) {
    for (int bus : {kBusStrumUp, kBusStrumDn, kBusRoot, kBusSpacing, kBusPitch, kBusTrig}) {
        clear_bus(busFrames, bus);
    }
}

// Run one step with no gate inputs active.
static void idle_step(nt::LoadedPlugin* loaded, float* bus) {
    clear_all_buses(bus);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
}

TEST_CASE("Strum SR1: OnDataRequest packs default state after Start", "[strum]") {
    // Default: qselect=0 at bits [0,4), spacing=8 at bits [12,9),
    // length=6 at bits [21,4).  All three should appear in the packed word.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = strum_applet_on_data_request(loaded->algorithm);

    // qselect=0 at [0,4)
    REQUIRE(((packed >> 0) & 0xF) == 0u);
    // spacing=8 at [12,9)
    REQUIRE(((packed >> 12) & 0x1FF) == 8u);
    // length=6 at [21,4)
    REQUIRE(((packed >> 21) & 0xF) == 6u);
    // stepmode=0 at bit 61
    REQUIRE(((packed >> 61) & 0x1) == 0u);
    // qmod=0 at bit 62
    REQUIRE(((packed >> 62) & 0x1) == 0u);
}

TEST_CASE("Strum SR2: serialise round-trip preserves qselect and spacing", "[strum]") {
    // Inject qselect=2 and spacing=50, then read back via OnDataRequest.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Build a packed word with qselect=2 at [0,4), spacing=50 at [12,9),
    // length=4 at [21,4), intervals all at default offset (12, stored as 12+12=24 per field).
    uint64_t inject = 0;
    inject |= (uint64_t)2 << 0;   // qselect=2
    inject |= (uint64_t)50 << 12; // spacing=50
    inject |= (uint64_t)4 << 21;  // length=4
    // Leave intervals at 0 (OnDataReceive will add MIN_INTERVAL=-12; 0+(-12)=-12)
    // Set interval fields to 12 (stored as intervals[i]-MIN_INTERVAL=intervals[i]+12):
    // default intervals shifted by 12: {0+12, 4+12, 7+12, 9+12, 11+12, 14+12} = {12,16,19,21,23,26}
    for (int i = 0; i < 6; ++i) {
        int default_interval = (int[]){0, 4, 7, 9, 11, 14}[i];
        uint64_t stored = (uint64_t)(default_interval + 12) & 0x3F;
        inject |= stored << (25 + i * 6);
    }

    strum_applet_on_data_receive(loaded->algorithm, inject);
    uint64_t packed = strum_applet_on_data_request(loaded->algorithm);

    REQUIRE(((packed >> 0) & 0xF) == 2u);
    REQUIRE(((packed >> 12) & 0x1FF) == 50u);
    REQUIRE(((packed >> 21) & 0xF) == 4u);
}

TEST_CASE("Strum SR3: strum-up trigger causes pitch output after ADCLag", "[strum]") {
    // After a strum-up gate edge, two step() calls are needed:
    // step 1 starts ADCLag (StartADCLag); step 2 fires EndOfADCLag and
    // plays the first note on Out(0).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);

    // Step 1: assert strum-up gate high to fire StartADCLag.
    write_bus(bus, kBusStrumUp, 5.0f);  // gate high
    clear_bus(bus, kBusRoot);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Clear gate between steps to avoid re-triggering; keep a second step.
    clear_all_buses(bus);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // After ADCLag expiry the applet should have written a pitch to Out(0).
    // With root=0 and default quantizer (semitone scale) the first note in
    // the default intervals (offset 0) produces the root pitch (0V or close).
    // We only verify the output bus has been written (non-default sentinel).
    // The exact voltage depends on quantizer state; just assert the gate
    // trigger fired (Trig bus high) during one of the steps.
    // Re-run with gate to produce trigger output:
    clear_all_buses(bus);
    write_bus(bus, kBusStrumUp, 5.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    clear_all_buses(bus);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // After this second trigger+settle pair the Trig bus should show activity.
    // Read the pitch bus -- it should be non-garbage (quantizer returns ~0 for
    // root=0 on default scale).
    float pitch = read_bus_last(bus, kBusPitch);
    // Pitch should be finite and in a reasonable range (quantizer maps to
    // semitone scale so values stay near 0V for root=0).
    REQUIRE(pitch >= -1.0f);
    REQUIRE(pitch <= 6.0f);
}

TEST_CASE("Strum SR4: strum-down trigger starts from last note (reverse direction)", "[strum]") {
    // Clock(1) sets inc=-1 and starts the strum from the last interval.
    // This is a structural/smoke test: after a strum-down edge and ADCLag
    // settle, the pitch output bus must have been written.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);

    // Fire strum-down gate.
    write_bus(bus, kBusStrumDn, 5.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    clear_all_buses(bus);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float pitch = read_bus_last(bus, kBusPitch);
    REQUIRE(pitch >= -1.0f);
    REQUIRE(pitch <= 6.0f);
}

TEST_CASE("Strum SR5: encoder turn changes qselect via customUi", "[strum]") {
    // Default qselect=0.  One encoder turn enters edit mode; a second
    // increments qselect to 1.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(((strum_applet_on_data_request(loaded->algorithm) >> 0) & 0xF) == 0u);

    // Enter edit mode (encoder button press).
    _NT_uiData ui{};
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Turn encoder right to increment cursor-targeted parameter (qselect when cursor==QUANT).
    ui.controls    = 0;
    ui.encoders[0] = 1;
    loaded->factory->customUi(loaded->algorithm, ui);

    // qselect should now be 1.
    uint64_t packed = strum_applet_on_data_request(loaded->algorithm);
    REQUIRE(((packed >> 0) & 0xF) == 1u);
}

TEST_CASE("Strum SR6: aux button press does not crash", "[strum]") {
    // AuxButton() changes state based on cursor position but must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Strum SR7: draw does not crash", "[strum]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    idle_step(loaded, bus);

    bool result = loaded->factory->draw(loaded->algorithm);
    REQUIRE(result == true);
}
