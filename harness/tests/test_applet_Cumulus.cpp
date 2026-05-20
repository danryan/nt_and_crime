// Per-applet pilot test skeleton: Cumulus.
//
// Manifest: shim/include/applet_manifests/Cumulus.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Cumulus.h
//
// Test concerns the implementer MUST address:
// - 10x ticks-per-step is the canonical gotcha for this applet. Cumulus
//   advances counters inside if (Clock(ch)). Per CLAUDE.md "10x clocked
//   multiplier", bus-level fire-count assertions MUST either model the
//   multiplier explicitly (see Cumulus CU2 in test_hemispheres.cpp:1264)
//   or drop to round-trip + state-injection only. Acknowledge in test
//   header before writing assertions.
// - Round-trip: pack_cumulus helper exists in
//   harness/tests/applet_test_helpers.cpp. Mirror byte-by-byte. Note
//   Cumulus zeroes bits 11..12 in OnDataRequest (per pack helper rules
//   in CLAUDE.md "Pack helper convention").
// - Behavior: gate input drives accumulation; CV output tracks the
//   accumulator. Verify the per-applet runtime step wrapper handles
//   vendor-frame globals correctly under standalone bus I/O.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

TEST_CASE("Cumulus placeholder", "[per-applet-pilot][cumulus]") {
    // TODO(implementer): exercise 10x-aware accumulation. State the
    // chosen coverage shape (explicit-10x or round-trip + state-injection)
    // in a comment at the top of the assertions block.
    REQUIRE(true);
}
