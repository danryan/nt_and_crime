// Output-parity class: float-using, 1-LSB tolerance on int16 output.
// dep-vec-osc invariant tests.
//
// VectorOscillator tests use SetPhaseIncrement(0x10000) against a Sine
// library waveform with SetScale(32767). The first 16 samples are pinned;
// any regression in interpolation math or waveform segment encoding must
// break these values. Tolerance is +/-1 LSB to accommodate host-vs-ARM
// rounding differences.
//
// RelabiManager tests use the singleton directly. They test
// WriteValues/ReadValues and WriteGates/ReadGates byte-identical round-trips
// (integer class, zero tolerance), IsLinked timing semantics, and
// Unload behaviour.

#include "catch.hpp"
#include "vector_osc/vec_osc_prereqs.h"
#include "vector_osc/HSVectorOscillator.h"
#include "vector_osc/WaveformManager.h"
#include "HSRelabiManager.h"

// ---------------------------------------------------------------------------
// VectorOscillator invariant tests
// ---------------------------------------------------------------------------

TEST_CASE("VectorOscillator: Sine waveform construction via WaveformManager", "[dep-vec-osc]") {
    WaveformManager::Validate();
    VectorOscillator osc = WaveformManager::VectorOscillatorFromWaveform(HS::Sine);

    SECTION("segment count and total time are non-zero after construction") {
        // Sine has 12 segments in the library (see waveform_library.h).
        CHECK(osc.SegmentCount() == 12);
        CHECK(osc.TotalTime() > 0);
    }
}

TEST_CASE("VectorOscillator: first 16 samples match pinned values at SetPhaseIncrement(0x10000)", "[dep-vec-osc]") {
    // Pinned expected values computed on first host run and committed as the
    // reference. Tolerance is +/-1 LSB.
    //
    // Configuration: Sine library waveform, SetScale(32767),
    // SetPhaseIncrement(0x10000), Reset() then 256 Next() calls.
    // Phase increment 0x10000 is tiny relative to the 32-bit phase space so
    // the oscillator traverses only the initial segment ramp over 256 calls.
    static const int16_t expected[16] = {
        1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, 34, 36, 39, 42, 45
    };

    WaveformManager::Validate();
    VectorOscillator osc = WaveformManager::VectorOscillatorFromWaveform(HS::Sine);
    osc.SetScale(32767);
    osc.SetPhaseIncrement(0x10000);
    osc.Reset();

    int16_t samples[256];
    for (int i = 0; i < 256; i++) {
        samples[i] = static_cast<int16_t>(osc.Next());
    }

    for (int i = 0; i < 16; i++) {
        INFO("sample index " << i);
        CHECK(samples[i] >= expected[i] - 1);
        CHECK(samples[i] <= expected[i] + 1);
    }
}

TEST_CASE("VectorOscillator: Reset() restores phase and allows re-run from the same start", "[dep-vec-osc]") {
    WaveformManager::Validate();
    VectorOscillator osc = WaveformManager::VectorOscillatorFromWaveform(HS::Sine);
    osc.SetScale(32767);
    osc.SetPhaseIncrement(0x10000);

    osc.Reset();
    int16_t run1[8];
    for (int i = 0; i < 8; i++) run1[i] = static_cast<int16_t>(osc.Next());

    osc.Reset();
    int16_t run2[8];
    for (int i = 0; i < 8; i++) run2[i] = static_cast<int16_t>(osc.Next());

    for (int i = 0; i < 8; i++) {
        INFO("index " << i);
        CHECK(run1[i] == run2[i]);
    }
}

TEST_CASE("VectorOscillator: GetNextWaveform navigates library waveforms", "[dep-vec-osc]") {
    WaveformManager::Validate();
    // Advance one step from Sine (32) in positive direction.
    uint8_t next = WaveformManager::GetNextWaveform(HS::Sine, 1);
    CHECK(next == static_cast<uint8_t>(HS::Cosine));

    // Advance one step back from Sine.
    uint8_t prev = WaveformManager::GetNextWaveform(HS::Sine, -1);
    CHECK(prev == static_cast<uint8_t>(HS::Ramp));
}

// ---------------------------------------------------------------------------
// RelabiManager invariant tests
// ---------------------------------------------------------------------------

TEST_CASE("RelabiManager: WriteValues/ReadValues round-trip is byte-identical", "[dep-vec-osc]") {
    OC::CORE::ticks = 100;
    RelabiManager& rm = RelabiManager::get();
    rm.Register(LEFT_HEMISPHERE);
    rm.Register(RIGHT_HEMISPHERE);

    rm.WriteValues(42, -7, 1000);
    int v1 = 0, v2 = 0, v3 = 0;
    rm.ReadValues(v1, v2, v3);

    CHECK(v1 == 42);
    CHECK(v2 == -7);
    CHECK(v3 == 1000);
}

TEST_CASE("RelabiManager: WriteGates/ReadGates round-trip is byte-identical", "[dep-vec-osc]") {
    OC::CORE::ticks = 100;
    RelabiManager& rm = RelabiManager::get();
    rm.Register(LEFT_HEMISPHERE);
    rm.Register(RIGHT_HEMISPHERE);

    bool gates_in[3] = {true, false, true};
    rm.WriteGates(gates_in);

    bool gates_out[3] = {false, false, false};
    rm.ReadGates(gates_out);

    CHECK(gates_out[0] == true);
    CHECK(gates_out[1] == false);
    CHECK(gates_out[2] == true);
}

TEST_CASE("RelabiManager: IsLinked is true immediately after both hemispheres register", "[dep-vec-osc]") {
    OC::CORE::ticks = 200;
    RelabiManager& rm = RelabiManager::get();
    rm.Register(LEFT_HEMISPHERE);
    rm.Register(RIGHT_HEMISPHERE);

    // Ticks unchanged; both registrations are within the 160-tick window.
    CHECK(rm.IsLinked() == true);
}

TEST_CASE("RelabiManager: IsLinked is false after Unload", "[dep-vec-osc]") {
    OC::CORE::ticks = 300;
    RelabiManager& rm = RelabiManager::get();
    rm.Register(LEFT_HEMISPHERE);
    rm.Register(RIGHT_HEMISPHERE);
    CHECK(rm.IsLinked() == true);

    rm.Unload(LEFT_HEMISPHERE);
    rm.Unload(RIGHT_HEMISPHERE);

    // Advance ticks far enough that the registrations are stale.
    OC::CORE::ticks = 1000;
    CHECK(rm.IsLinked() == false);
}

TEST_CASE("RelabiManager: IsLinked is false when only one hemisphere registers", "[dep-vec-osc]") {
    OC::CORE::ticks = 400;
    RelabiManager& rm = RelabiManager::get();
    rm.Unload(LEFT_HEMISPHERE);
    rm.Unload(RIGHT_HEMISPHERE);

    // Only register left side; right remains unloaded (registered[1] = 0).
    OC::CORE::ticks = 500;
    rm.Register(LEFT_HEMISPHERE);

    // Ticks well beyond the 160-tick window for right hemisphere (which has
    // registered[1] = 0, so t - 0 = 500 >= 160).
    CHECK(rm.IsLinked() == false);
}
