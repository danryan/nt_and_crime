// dep-cv-map invariant tests (Phase 5 Layer 1).
// Output-parity class: integer-only across CVInputMap, bjorklund, and
// clkdivmult. All call sites have integer inputs and integer outputs.
//
// Three groups:
//   [cvmap]     CVInputMap::In() round-trips frame.inputs at default
//               attenuversion=60 (Atten(60)=1000=100% passthrough).
//   [bjorklund] EuclideanPattern() returns byte-identical table lookups.
//   [clkdivmult] ClkDivMult clock-divide-by-2 fires once per two input ticks.
#include "catch.hpp"
#include "CVInputMap.h"
#include "util/clkdivmult.h"
#include "OC_core.h"

// ============================================================
// [cvmap] CVInputMap::In() round-trip
// ============================================================

TEST_CASE("cvmap: In() returns frame.inputs[0] at 1.0V with default attenuversion",
          "[dep-cv-map][cvmap]") {
    // cvmap[0] is initialized in globals.cpp as CVInputMap(0),
    // giving source=1 (ADC channel 0), attenuversion=60.
    // Atten(60) = 10 * 60 * 60 / 36 = 1000 (100% passthrough).
    // ONE_OCTAVE = 1536 hem units per volt.
    HS::frame.inputs[0] = 1536;  // 1.0V
    REQUIRE(cvmap[0].In() == 1536);
}

TEST_CASE("cvmap: In() returns default_value=0 when source==0 (unmapped)",
          "[dep-cv-map][cvmap]") {
    CVInputMap unmapped;  // default constructor: source=0
    HS::frame.inputs[0] = 1536;
    REQUIRE(unmapped.In() == 0);
}

TEST_CASE("cvmap: In() returns 0 at 0V", "[dep-cv-map][cvmap]") {
    HS::frame.inputs[0] = 0;
    REQUIRE(cvmap[0].In() == 0);
}

TEST_CASE("cvmap: In() round-trips negative CV (inverted signal)",
          "[dep-cv-map][cvmap]") {
    // -1.0V = -1536 hem units. Atten(60)=1000, -1536 * 1000 / 1000 = -1536.
    HS::frame.inputs[0] = -1536;
    REQUIRE(cvmap[0].In() == -1536);
}

TEST_CASE("cvmap: ChangeSource clamps to [0, CVMAP_ADC_LAST]",
          "[dep-cv-map][cvmap]") {
    CVInputMap m(0);  // source=1
    m.ChangeSource(-2);  // source=1 + (-2) = -1, clamped to 0
    REQUIRE(m.source == 0);
    m.ChangeSource(100);  // source=0 + 100 = 100, clamped to CVMAP_ADC_LAST=4
    REQUIRE(m.source == CVMAP_ADC_LAST);
}

TEST_CASE("cvmap: Pack/Unpack round-trip preserves source and attenuversion",
          "[dep-cv-map][cvmap]") {
    CVInputMap m(2);  // source=3
    m.attenuversion = -30;
    uint16_t packed = m.Pack();
    CVInputMap m2;
    m2.Unpack(packed);
    REQUIRE(m2.source == m.source);
    REQUIRE(m2.attenuversion == m.attenuversion);
}

// ============================================================
// [bjorklund] EuclideanPattern table lookup
// ============================================================

TEST_CASE("bjorklund: 3-of-8 Euclidean pattern is byte-identical to vendor table",
          "[dep-cv-map][bjorklund]") {
    // Vendor table index: ((8-2)*33 + 3) = 201.
    // Binary: 01001001 = 0x49. Active beats at positions 0, 3, 6 of 8 steps.
    // Rotation=0, padding=0.
    uint32_t pattern = EuclideanPattern(8, 3, 0);
    REQUIRE(pattern == 0x49u);
}

TEST_CASE("bjorklund: 4-of-8 Euclidean pattern is byte-identical to vendor table",
          "[dep-cv-map][bjorklund]") {
    // 4 evenly-spaced beats in 8 steps: 10101010 = 0x55.
    uint32_t pattern = EuclideanPattern(8, 4, 0);
    REQUIRE(pattern == 0x55u);
}

TEST_CASE("bjorklund: EuclideanFilter correctly identifies active steps",
          "[dep-cv-map][bjorklund]") {
    // 3-of-8 pattern 0x49 = 01001001. Active at clock 0, 3, 6.
    // clock 0: (0x49 >> 0) & 1 = 1 (true)
    // clock 1: (0x49 >> 1) & 1 = 0 (false)
    // clock 3: (0x49 >> 3) & 1 = 1 (true)
    REQUIRE(EuclideanFilter(8, 3, 0, 0) == true);
    REQUIRE(EuclideanFilter(8, 3, 0, 1) == false);
    REQUIRE(EuclideanFilter(8, 3, 0, 3) == true);
    REQUIRE(EuclideanFilter(8, 3, 0, 6) == true);
    REQUIRE(EuclideanFilter(8, 3, 0, 7) == false);
}

TEST_CASE("bjorklund: EuclideanFilter wraps clock modulo num_steps",
          "[dep-cv-map][bjorklund]") {
    // clock=8 wraps to 0, which is active in 3-of-8.
    REQUIRE(EuclideanFilter(8, 3, 0, 8) == EuclideanFilter(8, 3, 0, 0));
}

// ============================================================
// [clkdivmult] ClkDivMult clock division
// ============================================================

TEST_CASE("clkdivmult: divide-by-2 fires once per two input ticks",
          "[dep-cv-map][clkdivmult]") {
    // ClkDivMult with steps=2: fires on clock_count==1 (every 2 input ticks).
    // 10 input ticks -> 5 output fires.
    ClkDivMult d;
    d.Set(2);

    int fires = 0;
    for (int i = 0; i < 10; ++i) {
        // Advance OC::CORE::ticks so cycle_time is consistent.
        OC::CORE::ticks = static_cast<uint32_t>(i * 100);
        if (d.Tick(true)) ++fires;
    }
    REQUIRE(fires == 5);
}

TEST_CASE("clkdivmult: divide-by-1 fires on every input tick",
          "[dep-cv-map][clkdivmult]") {
    ClkDivMult d;
    d.Set(1);

    int fires = 0;
    for (int i = 0; i < 10; ++i) {
        OC::CORE::ticks = static_cast<uint32_t>(i * 100);
        if (d.Tick(true)) ++fires;
    }
    REQUIRE(fires == 10);
}

TEST_CASE("clkdivmult: steps=0 never fires", "[dep-cv-map][clkdivmult]") {
    ClkDivMult d;
    d.Set(0);

    int fires = 0;
    for (int i = 0; i < 10; ++i) {
        OC::CORE::ticks = static_cast<uint32_t>(i * 100);
        if (d.Tick(true)) ++fires;
    }
    REQUIRE(fires == 0);
}

TEST_CASE("clkdivmult: Reset clears clock_count and next_clock",
          "[dep-cv-map][clkdivmult]") {
    ClkDivMult d;
    d.Set(4);
    OC::CORE::ticks = 100;
    d.Tick(true);  // clock_count becomes 1
    d.Reset();
    REQUIRE(d.clock_count == 0);
    REQUIRE(d.next_clock == 0);
}
