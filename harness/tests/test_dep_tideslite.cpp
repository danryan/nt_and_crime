// Output-parity class: mixed (integer-only for ComputePhaseIncrement and
// PhaseExtractor; integer outputs for ProcessSample, pinned on host and
// asserted byte-identical).
//
// dep-tideslite: tideslite + PhaseExtractor invariant tests (Phase 5).
//
// Build note: tideslite.cpp defines WarpPhase / ShapePhase / ProcessSample as
// non-inline free functions. The dep test binary does not link tideslite.cpp
// directly (Makefile rule only links Hemispheres.host.o which does not yet
// register any tideslite-using applet). Including tideslite_impl.h pulls the
// implementation into this translation unit instead.

#include "catch.hpp"
#include <cstring>  // memset
#include <new>      // placement new

#include "tideslite/tideslite_impl.h"
#include "util/util_phase_extractor.h"

// ---------------------------------------------------------------------------
// ComputePhaseIncrement: integer-only, byte-identical class
// ---------------------------------------------------------------------------

TEST_CASE("ComputePhaseIncrement(0) matches pinned lut[0]", "[dep-tideslite]") {
    // pitch=0 is the lowest supported pitch (C-1 ~8.18 Hz at 16666 Hz sample
    // rate). The result comes directly from lut_increments[0] = 2106971 with
    // no shift applied.
    constexpr uint32_t expected = 2106971;
    CHECK(ComputePhaseIncrement(0) == expected);
}

TEST_CASE("ComputePhaseIncrement pinned for C4 (pitch=7680)", "[dep-tideslite]") {
    // vendor pitch units: 128 units per semitone, 1536 per octave.
    // pitch=7680 = 60 semitones * 128 = C4 in vendor scale.
    // Pinned from host run: 67423072.
    constexpr int16_t c4_pitch = 60 * 128;
    constexpr uint32_t expected = 67423072u;
    CHECK(ComputePhaseIncrement(c4_pitch) == expected);
}

// ---------------------------------------------------------------------------
// ComputePitch: round-trip
// ---------------------------------------------------------------------------

TEST_CASE("ComputePitch round-trips ComputePhaseIncrement for C4", "[dep-tideslite]") {
    constexpr int16_t c4_pitch = 60 * 128;  // 7680
    uint32_t pi = ComputePhaseIncrement(c4_pitch);
    int16_t recovered = ComputePitch(pi);
    // Round-trip tolerance: binary search in lut gives the nearest index *16,
    // so precision is within one lut step (16 pitch units = 1/8 semitone).
    CHECK(recovered == c4_pitch);
}

TEST_CASE("ComputePitch round-trips ComputePhaseIncrement for pitch=0", "[dep-tideslite]") {
    uint32_t pi = ComputePhaseIncrement(0);
    int16_t recovered = ComputePitch(pi);
    CHECK(recovered == 0);
}

// ---------------------------------------------------------------------------
// ProcessSample: integer output, pinned on host
// ---------------------------------------------------------------------------

TEST_CASE("ProcessSample(slope=0x8000, shape=0x8000, fold=0, phase=0x40000000) pinned",
          "[dep-tideslite]") {
    // Parameters chosen to be representative mid-range values:
    //   slope = 0x8000 (50% duty)
    //   shape = 0x8000 (50% wave shape)
    //   fold  = 0      (no folding)
    //   phase = 0x40000000 (25% of full cycle)
    //
    // Pinned from host run: unipolar=63131, bipolar=32513, flags=FLAG_EOR(2).
    TidesLiteSample s;
    ProcessSample(0x8000u, 0x8000u, 0, 0x40000000u, s);
    CHECK(s.unipolar == 63131u);
    CHECK(s.bipolar == 32513);
    CHECK(s.flags == FLAG_EOR);
}

TEST_CASE("ProcessSample flags: phase before EOA boundary sets FLAG_EOR", "[dep-tideslite]") {
    // eoa = slope << 16. With slope=0x4000, eoa = 0x40000000.
    // phase=0x3fffffff < eoa => FLAG_EOR set.
    TidesLiteSample s;
    ProcessSample(0x4000u, 0x8000u, 0, 0x3fffffffu, s);
    CHECK(s.flags & FLAG_EOR);
    CHECK(!(s.flags & FLAG_EOA));
}

TEST_CASE("ProcessSample flags: phase after EOA boundary sets FLAG_EOA", "[dep-tideslite]") {
    // phase=0x40000001 > eoa=0x40000000 => FLAG_EOA set.
    TidesLiteSample s;
    ProcessSample(0x4000u, 0x8000u, 0, 0x40000001u, s);
    CHECK(s.flags & FLAG_EOA);
    CHECK(!(s.flags & FLAG_EOR));
}

// ---------------------------------------------------------------------------
// PhaseExtractor: integer-only, deterministic after warm-up
// ---------------------------------------------------------------------------

// Helpers for zero-initialized placement-new to avoid uninitialized state
// (Init() does not zero all members; zero-init + Init() is safe).
template <size_t H, uint8_t M>
static PhaseExtractor<H, M>* make_pe(void* buf, size_t sz) {
    std::memset(buf, 0, sz);
    return new (buf) PhaseExtractor<H, M>();
}

TEST_CASE("PhaseExtractor: stable phase advance after warm-up with period=100",
          "[dep-tideslite]") {
    // Use small template params for fast iteration.
    using PE = PhaseExtractor<4, 4>;
    alignas(PE) char buf[sizeof(PE)];
    PE* pe = make_pe<4, 4>(buf, sizeof(PE));
    pe->Init();

    const int period = 100;

    // Issue a reset+clock to bring internal state to a clean baseline.
    pe->Advance(true, true, 0);

    // First cycle: clock + (period-1) non-clock ticks. Predictor not yet
    // trained; phase does not advance (next_clock_tick=0 from predictor).
    pe->Advance(true, false, 0);
    for (int t = 1; t < period; t++) pe->Advance(false, false, 0);

    // Cycles 2-N are stable (predictor has converged).
    // Pin values from host run: cycle start=43383508, t=25=1127971208,
    // t=50=2212558908, t=75=3297146608.
    const uint32_t expected_clock = 43383508u;
    const uint32_t expected_q1    = 1127971208u;
    const uint32_t expected_q2    = 2212558908u;
    const uint32_t expected_q3    = 3297146608u;

    // Run two stable cycles and verify each produces the same pinned values.
    for (int cycle = 0; cycle < 2; cycle++) {
        uint32_t at_clock = pe->Advance(true, false, 0);
        uint32_t at_q1 = 0, at_q2 = 0, at_q3 = 0;
        for (int t = 1; t < period; t++) {
            uint32_t p = pe->Advance(false, false, 0);
            if (t == 25) at_q1 = p;
            if (t == 50) at_q2 = p;
            if (t == 75) at_q3 = p;
        }
        INFO("cycle " << cycle);
        CHECK(at_clock == expected_clock);
        CHECK(at_q1    == expected_q1);
        CHECK(at_q2    == expected_q2);
        CHECK(at_q3    == expected_q3);
    }
}

TEST_CASE("PhaseExtractor: two stable cycles produce identical phase sequences",
          "[dep-tideslite]") {
    // Additional coverage: verify that the stable-cycle invariant holds for a
    // third independent run with a different period (period=50). This confirms
    // the predictor convergence is not coincidental with the period=100 case.
    using PE = PhaseExtractor<4, 4>;
    alignas(PE) char buf[sizeof(PE)];
    PE* pe = make_pe<4, 4>(buf, sizeof(PE));
    pe->Init();

    const int period = 50;

    pe->Advance(true, true, 0);

    // First cycle to warm the predictor.
    pe->Advance(true, false, 0);
    for (int t = 1; t < period; t++) pe->Advance(false, false, 0);

    // Capture cycle 2 values.
    uint32_t c2_clock = pe->Advance(true, false, 0);
    uint32_t c2_q1 = 0, c2_q2 = 0;
    for (int t = 1; t < period; t++) {
        uint32_t p = pe->Advance(false, false, 0);
        if (t == period / 4) c2_q1 = p;
        if (t == period / 2) c2_q2 = p;
    }

    // Cycle 3 must match cycle 2 exactly.
    uint32_t c3_clock = pe->Advance(true, false, 0);
    uint32_t c3_q1 = 0, c3_q2 = 0;
    for (int t = 1; t < period; t++) {
        uint32_t p = pe->Advance(false, false, 0);
        if (t == period / 4) c3_q1 = p;
        if (t == period / 2) c3_q2 = p;
    }

    CHECK(c3_clock == c2_clock);
    CHECK(c3_q1    == c2_q1);
    CHECK(c3_q2    == c2_q2);
}
