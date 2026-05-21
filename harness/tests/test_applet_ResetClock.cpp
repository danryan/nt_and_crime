// Per-applet test: ResetClock.
//
// Manifest: shim/include/applet_manifests/ResetClock.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/ResetClock.h
//
// 10x ticks-per-step concern: ACKNOWLEDGED. This is the applet that originated
// the spec-mismatch abort report (docs/superpowers/abort-reports/
// 2026-05-18-resetclock-spec-mismatch.md). The Controller() increments
// ticks_since_clock UNCONDITIONALLY each inner tick; with 10 inner ticks per
// buffer the counter advances 10x per step(). The pending_clocks counter also
// increments 10 times when Clock(0) is seen across all 10 inner ticks of one
// buffer. Bus-level fire-count assertions are UNSOUND for this applet.
//
// Test shape: STATE-INJECTION ONLY via OnDataRequest / OnDataReceive for
// serialised fields (length, offset, spacing). Timing-gated behavior is tested
// using hem_shim::inner_ticks_override=1 (one Controller() tick per step()
// call) so tick-count arithmetic is exact and predictable. Fire-count
// assertions at bus level are NEVER written.
//
// Vendor serialisation layout (OnDataRequest):
//   bits [0,5)   = length - 1   (5 bits; default length=8 -> packed=7)
//   bits [5,10)  = offset        (5 bits; default 0)
//   bits [10,17) = spacing       (7 bits; default 6)
//
// Not serialised (runtime state):
//   ticks_since_clock, pending_clocks, position, offset_mod.
//
// Bus parameter layout (per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Clock"    (default 1, gate)
//   v[1]  = input  bus for "Reset"    (default 2, gate)
//   v[2]  = input  bus for "Offset"   (default 3, cv)
//   v[3]  = output bus for "Advance"  (default 13, gate)
//   v[4]  = output mode for "Advance" (default 1 = replace)
//   v[5]  = output bus for "Trigger"  (default 14, gate)
//   v[6]  = output mode for "Trigger" (default 1 = replace)
//
// Vendor timing (with spacing=1, RC_TICKS_PER_MS=175):
//   Output fires when: pending_clocks > 0 && ticks_since_clock > 175.
//   With inner_ticks_override=1 per step():
//     - Clock edge tick: pending_clocks++, condition check fails (0 > 175),
//       ticks_since_clock becomes 1.
//     - Ticks 1..175: ticks_since_clock increments each tick.
//     - Tick 176 (step index 176 from edge): condition 176 > 175 fires.
//   So: 1 step for edge + 176 more single-tick steps = fire on step 176
//   (counting from 0, where step 0 is the edge step).

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/ResetClock.cpp.
uint64_t resetclock_applet_on_data_request(_NT_algorithm* self);
void     resetclock_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// inner_ticks_override: set to 1 to run exactly one Controller() tick per
// step() call. The runtime resets it to 0 after consuming it, so it must be
// set before each step() call that needs single-tick semantics.
namespace hem_shim { extern int inner_ticks_override; }

// Bus indices for ResetClock's default parameter layout.
static constexpr int kBusClock   = 1;   // v[0] default - clock gate input
static constexpr int kBusReset   = 2;   // v[1] default - reset gate input
static constexpr int kBusOffset  = 3;   // v[2] default - offset CV input
static constexpr int kBusAdvance = 13;  // v[3] default - Advance gate output
static constexpr int kBusTrigger = 14;  // v[5] default - Trigger gate output

// RC_TICKS_PER_MS = HEMISPHERE_CLOCK_TICKS = 175 (vendor ResetClock.h:22).
static constexpr int kTicksPerMs = 175;

// Frames per step call matching the host sim default (numFramesBy4=8).
static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a rising-edge gate pulse on a 1-based bus (frame 0 high, rest low).
static void write_gate_pulse(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    dst[0] = 6.0f;
    for (int i = 1; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Clear a single bus to 0V.
static void clear_bus(float* busFrames, int bus_1based) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
}

// Returns true if the last frame of the gate output bus exceeds 0.5V.
static bool read_gate_bus(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)] > 0.5f;
}

// Pack ResetClock state matching vendor OnDataRequest encoding.
//   bits [0,5)  = length - 1  (5 bits, raw 0..31 -> length 1..32)
//   bits [5,10) = offset       (5 bits, raw 0..31)
//   bits [10,17)= spacing      (7 bits, raw 1..100)
static uint64_t pack_rc(int length, int offset, int spacing) {
    uint64_t d = 0;
    d |= (uint64_t)((length - 1) & 0x1F) << 0;
    d |= (uint64_t)(offset        & 0x1F) << 5;
    d |= (uint64_t)(spacing       & 0x7F) << 10;
    return d;
}

// Reset runtime, load the plugin, and return it.
static nt::LoadedPlugin* setup() {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    return loaded;
}

// Run N single-tick step() calls with zero bus input, discarding output.
// Used to advance ticks_since_clock toward the firing threshold.
static void run_single_ticks(nt::LoadedPlugin* loaded, float* bus, int n) {
    for (int i = 0; i < n; ++i) {
        hem_shim::inner_ticks_override = 1;
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

TEST_CASE("ResetClock RC1: OnDataRequest packs default state after Start",
          "[per-applet][resetclock]") {
    // Vendor Start(): length=8, offset=0, spacing=6.
    // Packed: bits[0,5)=7 (length-1), bits[5,10)=0, bits[10,17)=6.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = resetclock_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x1F)          == 7u);   // length-1=7
    REQUIRE(((packed >> 5) & 0x1F)  == 0u);   // offset=0
    REQUIRE(((packed >> 10) & 0x7F) == 6u);   // spacing=6
}

TEST_CASE("ResetClock RC2: serialise round-trip preserves length, offset, spacing",
          "[per-applet][resetclock]") {
    // Inject length=16, offset=3, spacing=12 and confirm round-trip.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_rc(16, 3, 12);
    resetclock_applet_on_data_receive(loaded->algorithm, state_in);

    uint64_t packed = resetclock_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x1F)          == 15u);  // length-1=15
    REQUIRE(((packed >> 5) & 0x1F)  == 3u);   // offset=3
    REQUIRE(((packed >> 10) & 0x7F) == 12u);  // spacing=12
}

TEST_CASE("ResetClock RC3: step runs without crash on silent buses",
          "[per-applet][resetclock]") {
    // Run several steps with all-zero buses. No assertion on timing output
    // since ticks_since_clock advances 10x per step with default tick count.
    // This test confirms the Controller/frame loop is safe with no input.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    float bus[32 * kNumFrames] = {};
    for (int i = 0; i < 5; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }
    REQUIRE(true);
}

TEST_CASE("ResetClock RC4: encoder turn changes length via customUi",
          "[per-applet][resetclock]") {
    // OnEncoderMove(1) with cursor=0 increments length from default 8 to 9.
    // First encoder press enters edit mode; second turn increments length.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    // Default length=8 (packed[0,5) = 7).
    REQUIRE((resetclock_applet_on_data_request(loaded->algorithm) & 0x1F) == 7u);

    // Enter edit mode.
    _NT_uiData ui_press{};
    ui_press.controls    = kNT_encoderButtonL;
    ui_press.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_press);

    // Turn encoder +1 in edit mode: length increments to 9.
    _NT_uiData ui_turn{};
    ui_turn.encoders[0] = 1;
    ui_turn.controls    = 0;
    ui_turn.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_turn);

    uint64_t packed = resetclock_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x1F) == 8u);  // length=9 -> length-1=8
}

TEST_CASE("ResetClock RC5: encoder button press does not crash",
          "[per-applet][resetclock]") {
    // ResetClock::OnButtonPress is inherited no-op. Confirm routing completes.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("ResetClock RC6: aux button press does not crash",
          "[per-applet][resetclock]") {
    // on_aux_button routes to OnButtonPress (no-op). Must not crash.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("ResetClock RC7: Advance output fires after threshold ticks with spacing=1",
          "[per-applet][resetclock]") {
    // With spacing=1 and inner_ticks_override=1:
    //   threshold = 1 * RC_TICKS_PER_MS = 175 ticks.
    //   Step 0 (edge): pending_clocks=1; condition (0 > 175) false;
    //     ticks_since_clock becomes 1.
    //   Steps 1..175 (175 more steps): ticks_since_clock grows from 1 to 176.
    //     On the 175th of these steps ticks_since_clock arrives at the check as
    //     175, which still fails (> not >=), incrementing to 176.
    //   Step 176 (the 176th step after edge): check sees 176 > 175. FIRES.
    //   Total: 1 edge step + 176 more = run_single_ticks(kTicksPerMs + 1).
    //
    // Bus-level fire-count is NOT asserted; we only observe output state
    // after the threshold is crossed.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    // Set spacing=1, length=8, offset=0.
    resetclock_applet_on_data_receive(loaded->algorithm, pack_rc(8, 0, 1));

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusClock);
    clear_bus(bus, kBusReset);
    clear_bus(bus, kBusAdvance);
    clear_bus(bus, kBusTrigger);

    // Step 0: deliver a rising-edge clock pulse. Single inner tick.
    write_gate_pulse(bus, kBusClock);
    hem_shim::inner_ticks_override = 1;
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Clear the clock bus so subsequent steps see no new edge.
    clear_bus(bus, kBusClock);

    // Advance output must be low immediately after the clock edge.
    REQUIRE(read_gate_bus(bus, kBusAdvance) == false);

    // Run kTicksPerMs + 1 = 176 more single-tick steps.
    // The firing step is step 175 of these (0-indexed), when ticks_since_clock
    // reaches 176 and the condition 176 > 175 becomes true.
    run_single_ticks(loaded, bus, kTicksPerMs + 1);

    REQUIRE(read_gate_bus(bus, kBusAdvance) == true);
}

TEST_CASE("ResetClock RC8: Trigger output fires alongside Advance when only one pending clock",
          "[per-applet][resetclock]") {
    // Vendor: when pending_clocks == 1 at the output cycle, both ClockOut(0)
    // and ClockOut(1) fire on the same tick. With spacing=1 and a single
    // Clock(0) edge, pending_clocks stays 1 until the output cycle. Both
    // Advance and Trigger must be high after threshold crossing.
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    resetclock_applet_on_data_receive(loaded->algorithm, pack_rc(8, 0, 1));

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusClock);
    clear_bus(bus, kBusReset);
    clear_bus(bus, kBusAdvance);
    clear_bus(bus, kBusTrigger);

    // Step 0: single clock edge. pending_clocks becomes 1.
    write_gate_pulse(bus, kBusClock);
    hem_shim::inner_ticks_override = 1;
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus, kBusClock);

    // Run kTicksPerMs + 1 = 176 more single-tick steps to cross threshold.
    // Firing step (step 175 of these) sees pending_clocks==1, so both
    // ClockOut(0) and ClockOut(1) fire.
    run_single_ticks(loaded, bus, kTicksPerMs + 1);

    REQUIRE(read_gate_bus(bus, kBusAdvance) == true);
    REQUIRE(read_gate_bus(bus, kBusTrigger) == true);
}

TEST_CASE("ResetClock RC9: serialise round-trip stability over boundary values",
          "[per-applet][resetclock]") {
    // Verify boundary values: length=1 (min), offset=0, spacing=1 (min).
    // Also verify length=32 (max), offset=31 (max for length=32), spacing=100 (max).
    nt::LoadedPlugin* loaded = setup();
    REQUIRE(loaded != nullptr);

    // Min bounds.
    resetclock_applet_on_data_receive(loaded->algorithm, pack_rc(1, 0, 1));
    uint64_t packed = resetclock_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x1F)          == 0u);   // length-1=0
    REQUIRE(((packed >> 5) & 0x1F)  == 0u);   // offset=0
    REQUIRE(((packed >> 10) & 0x7F) == 1u);   // spacing=1

    // Max bounds: offset must be < length, so use offset=31 with length=32.
    resetclock_applet_on_data_receive(loaded->algorithm, pack_rc(32, 31, 100));
    packed = resetclock_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x1F)          == 31u);  // length-1=31
    REQUIRE(((packed >> 5) & 0x1F)  == 31u);  // offset=31
    REQUIRE(((packed >> 10) & 0x7F) == 100u); // spacing=100
}
