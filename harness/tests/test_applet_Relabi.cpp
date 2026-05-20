// Per-applet pilot test skeleton: Relabi.
//
// Manifest: shim/include/applet_manifests/Relabi.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Relabi.h
//
// Test concerns the implementer MUST address:
// - Largest pilot (~4204 B unique text per solo probe). Validates the
//   per-applet pattern under worst-case text size.
// - Vendor deps: Relabi uses HSRelabiManager (header-only, vendor-located)
//   + HSVectorOscillator + WaveformManager. All accessible via
//   -Ivendor/.../applets + .. relative path. No shim re-export needed.
// - Round-trip: pack_relabi helper if it exists; otherwise mirror vendor
//   OnDataRequest byte-by-byte (RelabiManager state spans multiple
//   bitfields; check vendor source).
// - Behavior: RelabiManager state visible through CV output bus; assert
//   a known-state evolution produces expected output samples.
// - 10x ticks-per-step applies; sample observations must account for it.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

TEST_CASE("Relabi placeholder", "[per-applet-pilot][relabi]") {
    // TODO(implementer): exercise the RelabiManager surface. SegmentDisplay
    // text rendering is exercised at hardware smoke (no host render).
    REQUIRE(true);
}
