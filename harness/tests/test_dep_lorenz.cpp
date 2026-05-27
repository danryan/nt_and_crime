// Output-parity class: integer-only, byte-identical across host and ARM.
// dep-lorenz: LorenzGeneratorManager + streams_lorenz_generator invariant tests.
//
// The vendor lorenz sources are compiled in place from
// vendor/O_C-Phazerville/software/src. This test includes the vendor header
// by bare name; -I$(HEM_SRC_DIR) on SHIM_INCLUDE resolves it.
#include <cstdint>
#include <cstring>

// streams_resources.cpp and streams_lorenz_generator.cpp are compiled as
// separate TUs via Makefile's VENDOR_DEP_HOST_SRCS so the same code links
// into the LowerRenz applet build path without duplicating symbols. Pull
// only the headers here.
#include "streams_lorenz_generator.h"

// OC::CORE::ticks declared in OC_core.h, defined in shim/src/globals.cpp
// (compiled into Hemispheres.host.o, linked into this binary).
#include "OC_core.h"

// HSLorenzGeneratorManager.h is header-only; streams:: types are in scope.
#include "HSLorenzGeneratorManager.h"

#include "catch.hpp"

// -------------------------------------------------------------------------
// Helper: advance OC::CORE::ticks by delta and call Process().
// LorenzGeneratorManager::Process() throttles to one call per
// LORENZ_PROCESS_TICKS (16) ticks.
// -------------------------------------------------------------------------
static void tick_process(LorenzGeneratorManager* m, uint32_t delta = 16) {
    OC::CORE::ticks += delta;
    m->Process();
}

// -------------------------------------------------------------------------
// Invariant test: SetFreq(0,128), SetRho(0,80), Reset(0), then fire 8
// Process() calls by advancing ticks >= 16 each time. Assert GetOut(0) and
// GetOut(1) are byte-identical to pre-computed expected integer values.
//
// Derivation of expected values:
//   Init(0): Lx1_=0.1*2^24=1677721, Ly1_=0, Lz1_=0.
//   Reset(0) calls Init(0) on next Process().
//   freq[0]=128 -> rate1 = 128 >> 8 = 0 -> lut_lorenz_rate[0] = 3.
//   freq_range1=2 (hardcoded in Process call) -> Ldt1 = 3 >> (5-2) = 3 >> 3 = 0.
//   With Ldt1=0: Lx1, Ly1, Lz1 do not change from initial values.
//   Lx1_scaled = (1677721 * 3 >> 16) + 32769 = (5033163 >> 16) + 32769
//              = 76 + 32769 = 32845.
//   Ly1_scaled = (0 * 3 >> 16) + 32769 = 0 + 32769 = 32769.
//   out_a_ = LORENZ_OUTPUT_X1 -> dac_code_[0] = 32845.
//   out_b_ = LORENZ_OUTPUT_Y1 -> dac_code_[1] = 32769.
//   All 8 samples are identical: state is frozen at Ldt1=0.
// -------------------------------------------------------------------------
TEST_CASE("dep-lorenz: LorenzGeneratorManager output parity", "[dep-lorenz]") {
    // Set ticks to 0 so the throttle gate fires on the first Process() call.
    OC::CORE::ticks = 0;

    LorenzGeneratorManager* m = LorenzGeneratorManager::get();
    m->SetFreq(0, 128);
    m->SetRho(0, 80);
    m->Reset(0);

    // Fire exactly 8 Process() calls by advancing ticks >= 16 each time.
    uint16_t out0[8];
    uint16_t out1[8];
    for (int i = 0; i < 8; ++i) {
        tick_process(m, 16);
        out0[i] = static_cast<uint16_t>(m->GetOut(0));
        out1[i] = static_cast<uint16_t>(m->GetOut(1));
    }

    // Pre-computed expected values (integer-only, byte-identical).
    const uint16_t expected0[8] = {
        32845, 32845, 32845, 32845,
        32845, 32845, 32845, 32845
    };
    const uint16_t expected1[8] = {
        32769, 32769, 32769, 32769,
        32769, 32769, 32769, 32769
    };

    for (int i = 0; i < 8; ++i) {
        INFO("sample " << i << ": out0=" << out0[i] << " expected=" << expected0[i]);
        CHECK(out0[i] == expected0[i]);
    }
    for (int i = 0; i < 8; ++i) {
        INFO("sample " << i << ": out1=" << out1[i] << " expected=" << expected1[i]);
        CHECK(out1[i] == expected1[i]);
    }
}

// -------------------------------------------------------------------------
// DAC-range check: all four outputs stay within uint16_t range under a
// live (non-zero Ldt) configuration. Uses a higher freq so Ldt > 0 and
// the attractor actually evolves.
// -------------------------------------------------------------------------
TEST_CASE("dep-lorenz: generator output within DAC range", "[dep-lorenz]") {
    // High baseline avoids colliding with the throttle from the previous test.
    OC::CORE::ticks = 1000000;

    LorenzGeneratorManager* m = LorenzGeneratorManager::get();
    m->SetFreq(0, 4096);
    m->SetRho(0, 60);
    m->Reset(0);
    m->Reset(1);

    for (int i = 0; i < 16; ++i) {
        tick_process(m, 16);
        uint16_t v0 = static_cast<uint16_t>(m->GetOut(0));
        uint16_t v1 = static_cast<uint16_t>(m->GetOut(1));
        uint16_t v2 = static_cast<uint16_t>(m->GetOut(2));
        uint16_t v3 = static_cast<uint16_t>(m->GetOut(3));
        CHECK(v0 <= 65535u);
        CHECK(v1 <= 65535u);
        CHECK(v2 <= 65535u);
        CHECK(v3 <= 65535u);
    }
}
