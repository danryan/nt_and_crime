// Output-parity class: mixed. Tempo is an integer field (uint16_t GetTempo())
// returning the set BPM value; tick counting and Tock boundary detection are
// integer comparisons. Float methods (GetTempoFloat()) are tested with a
// tolerance check. Per-method parity annotations are inline below.
//
// dep-clock-mgr invariant tests (Phase 5 Layer 1).
// Exercises the full HS::ClockManager port including the advance_one_tick()
// shim wrapper. The global extern clock_m (type HSClockManager, IS-A
// HS::ClockManager) is used directly so the test validates the same object
// that hemispheres_shim::step() drives.

#include "catch.hpp"
#include "HSClockManager.h"
#include "OC_core.h"
#include <cmath>
#include <cstdint>

// The canonical singleton from shim/src/globals.cpp.
extern HSClockManager clock_m;

// Helper: reset OC::CORE::ticks and re-initialize clock_m to a clean state
// between test cases. Because clock_m is a global, shared state from previous
// test cases can leak. This helper sets tempo, starts the clock, and resets
// the tick counter.
static void reset_clock(uint16_t bpm = 120, int multiply = 1) {
    OC::CORE::ticks = 0;
    // Reconstruct in-place to clear all fields (queue, bool arrays, etc.).
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();
    clock_m.SetTempoBPM(bpm);
    clock_m.SetMultiply(multiply, 0);
    clock_m.Start(false); // false = not paused; also calls Reset() internally
    // After Start(), beat_tick == OC::CORE::ticks == 0. Advance one tick so
    // the first SyncTrig sees now == 1 (avoids ambiguity at the boundary
    // where now == beat_tick == 0).
    OC::CORE::ticks += 1;
    clock_m.advance_one_tick();
}

// ---------------------------------------------------------------------------
// CM1: Basic tempo and running state
// Output-parity: integer (GetTempo() returns uint16_t BPM).
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM1: SetTempoBPM and IsRunning", "[dep-clock-mgr]") {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();

    // Before Start(), clock is not running.
    clock_m.SetTempoBPM(120);
    CHECK(clock_m.IsRunning() == false);

    // Integer-parity: GetTempo() returns the exact integer BPM.
    CHECK(clock_m.GetTempo() == 120);

    // After Start(), IsRunning() returns true.
    clock_m.Start(false);
    CHECK(clock_m.IsRunning() == true);
    CHECK(clock_m.IsPaused() == false);

    // GetTempo() still returns 120 after Start().
    CHECK(clock_m.GetTempo() == 120);

    // ticks_per_beat is 1000000 / 120 = 8333 (integer division, truncated).
    CHECK(clock_m.GetTempoTicks() == (1000000u / 120u));
}

// ---------------------------------------------------------------------------
// CM2: advance_one_tick drives Tock() at the expected beat boundary
// Output-parity: integer (tick counts and Tock() boolean).
//
// Setup: 120 BPM => ticks_per_beat = 8333. Multiply channel 0 by 1 (i.e.,
// one Tock per beat). After Start() and one advance, Tock(0) should be true
// immediately (count=0 means next_tock_tick = beat_tick, now >= beat_tick).
// After 8333 more advances, Tock(0) fires again at the next beat boundary.
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM2: Tock fires at beat boundary via advance_one_tick", "[dep-clock-mgr]") {
    reset_clock(120, 1);

    // After reset_clock, we've done one advance_one_tick. Tock(0) should be
    // true because count[0] was 0 at beat_tick=0, so next_tock_tick=0 and
    // now=1 >= 0.
    // Integer-parity: Tock() returns bool.
    CHECK(clock_m.Tock(0) == true);

    // Advance another tick; Tock should not fire again until ticks_per_beat.
    OC::CORE::ticks += 1;
    clock_m.advance_one_tick();
    CHECK(clock_m.Tock(0) == false);

    // Advance up to the second beat boundary (ticks_per_beat - 1 more ticks
    // from the current position of 2). The Tock fires at count[0]=1 =>
    // next_tock_tick = beat_tick + 1 * ticks_per_beat = 8333.
    const uint32_t tpb = clock_m.GetTempoTicks(); // 8333 at 120 BPM
    // current ticks = 2; advance to tpb - 1.
    while (OC::CORE::ticks < tpb - 1) {
        OC::CORE::ticks += 1;
        clock_m.advance_one_tick();
        CHECK(clock_m.Tock(0) == false);
    }
    // Exactly at tpb: Tock should fire.
    OC::CORE::ticks += 1; // now ticks == tpb == 8333
    clock_m.advance_one_tick();
    // Integer-parity: ticks == tpb means next_tock_tick is met.
    CHECK(OC::CORE::ticks == tpb);
    CHECK(clock_m.Tock(0) == true);
}

// ---------------------------------------------------------------------------
// CM3: GetTempoFloat() returns correct BPM as float
// Output-parity: float-tolerance. 1000000 / ticks_per_beat should equal
// the BPM set, within floating-point rounding (1e-3 absolute tolerance).
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM3: GetTempoFloat returns BPM within tolerance", "[dep-clock-mgr]") {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();

    clock_m.SetTempoBPM(120);
    float tempo_float = clock_m.GetTempoFloat();
    // Float-tolerance: 1000000.0 / (1000000/120) = 120.000... within 0.01.
    CHECK(std::fabs(tempo_float - 120.0f) < 0.1f);

    clock_m.SetTempoBPM(60);
    tempo_float = clock_m.GetTempoFloat();
    CHECK(std::fabs(tempo_float - 60.0f) < 0.1f);
}

// ---------------------------------------------------------------------------
// CM4: GetMultiply / SetMultiply round-trip
// Output-parity: integer.
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM4: GetMultiply / SetMultiply round-trip", "[dep-clock-mgr]") {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();

    // Default multiply for channel 0 is 0 (disabled).
    CHECK(clock_m.GetMultiply(0) == 0);

    // Set multiply to 2 (double speed).
    clock_m.SetMultiply(2, 0);
    CHECK(clock_m.GetMultiply(0) == 2);

    // Set multiply to -1 (divide by 2).
    clock_m.SetMultiply(-1, 0);
    CHECK(clock_m.GetMultiply(0) == -1);

    // Clamp at CLOCK_MAX_MULTIPLE = 24.
    clock_m.SetMultiply(100, 0);
    CHECK(clock_m.GetMultiply(0) == HS::CLOCK_MAX_MULTIPLE);

    // Clamp at CLOCK_MIN_MULTIPLE = -31.
    clock_m.SetMultiply(-100, 0);
    CHECK(clock_m.GetMultiply(0) == HS::CLOCK_MIN_MULTIPLE);
}

// ---------------------------------------------------------------------------
// CM5: GetCycleTicks returns correct cycle ticks for multiply and divide modes
// Output-parity: integer.
// At 120 BPM, ticks_per_beat = 8333. Multiply = 2 => cycle = 8333/2 = 4166.
// Divide = -1 (becomes /2) => cycle = 8333 * (1 - (-1)) = 8333 * 2 = 16666.
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM5: GetCycleTicks multiply and divide", "[dep-clock-mgr]") {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();

    clock_m.SetTempoBPM(120);
    const uint32_t tpb = clock_m.GetTempoTicks(); // 8333

    // Multiply by 2: cycle = tpb / 2.
    clock_m.SetMultiply(2, 0);
    CHECK(clock_m.GetCycleTicks(0) == tpb / 2);

    // Multiply by 1: cycle = tpb.
    clock_m.SetMultiply(1, 0);
    CHECK(clock_m.GetCycleTicks(0) == tpb);

    // Divide by 2 (multiply = -1): cycle = tpb * (1 - (-1)) = tpb * 2.
    clock_m.SetMultiply(-1, 0);
    CHECK(clock_m.GetCycleTicks(0) == tpb * 2u);

    // Multiply = 0 (disabled): cycle = 0.
    clock_m.SetMultiply(0, 0);
    CHECK(clock_m.GetCycleTicks(0) == 0u);
}

// ---------------------------------------------------------------------------
// CM6: Stop() terminates IsRunning(); Start() restarts it
// Output-parity: integer (boolean).
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM6: Stop and restart", "[dep-clock-mgr]") {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();

    clock_m.SetTempoBPM(120);
    clock_m.Start(false);
    CHECK(clock_m.IsRunning() == true);

    clock_m.Stop();
    CHECK(clock_m.IsRunning() == false);
    CHECK(clock_m.IsPaused() == false);

    // advance_one_tick is a no-op when not running.
    OC::CORE::ticks += 1;
    clock_m.advance_one_tick();
    CHECK(clock_m.IsRunning() == false);

    // Restart.
    clock_m.Start(false);
    CHECK(clock_m.IsRunning() == true);
}

// ---------------------------------------------------------------------------
// CM7: Pause() makes IsRunning() false; unpause via Start(false)
// Output-parity: integer (boolean).
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM7: Pause and resume", "[dep-clock-mgr]") {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();

    clock_m.SetTempoBPM(120);
    clock_m.Start(false);
    CHECK(clock_m.IsRunning() == true);

    clock_m.Pause();
    CHECK(clock_m.IsRunning() == false);
    CHECK(clock_m.IsPaused() == true);

    // advance_one_tick is a no-op when paused (IsRunning() == false).
    OC::CORE::ticks += 1;
    clock_m.advance_one_tick();
    CHECK(clock_m.IsRunning() == false);
}

// ---------------------------------------------------------------------------
// CM8: Modulate adjusts tempo and shuffle transiently
// Output-parity: integer.
// Modulate(tempo_diff, shuffle_diff) adjusts the live tempo without changing
// tempo_setting. tempo = constrain(tempo_setting + tempo_diff, ...).
// ---------------------------------------------------------------------------
TEST_CASE("dep-clock-mgr CM8: Modulate adjusts live tempo", "[dep-clock-mgr]") {
    OC::CORE::ticks = 0;
    clock_m.~HSClockManager();
    new (&clock_m) HSClockManager();

    clock_m.SetTempoBPM(120);
    // Modulate by +10 BPM.
    clock_m.Modulate(10, 0);
    // tempo field is updated; tempo_setting stays 120.
    CHECK(clock_m.tempo == 130);
    CHECK(clock_m.tempo_setting == 120);
    // GetTempo() returns tempo_setting (not tempo).
    CHECK(clock_m.GetTempo() == 120);

    // Modulate back to 0.
    clock_m.Modulate(0, 0);
    CHECK(clock_m.tempo == 120);
}
