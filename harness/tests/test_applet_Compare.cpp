// Per-applet pilot test skeleton: Compare.
//
// Manifest: shim/include/applet_manifests/Compare.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Compare.h
//
// Test concerns the implementer MUST address:
// - Round-trip: Compare's vendor OnDataRequest() returns 0; assert this
//   directly rather than packing.
// - Behavior: drive known CV inputs through standalone bus paths and
//   assert the comparator output matches expected.
// - customUi: drive _NT_uiData with encoders[0] = 1 and confirm vendor
//   counter advances; drive kNT_encoderButtonL edge and confirm
//   OnButtonPress(); drive kNT_button1 edge and confirm aux-button.
// - No 10x ticks-per-step concern (no clock-driven state).

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

TEST_CASE("Compare placeholder", "[per-applet-pilot][compare]") {
    // TODO(implementer): load plugins/applets/Compare.cpp via the harness
    // loader and exercise the manifest-declared bus I/O surface.
    REQUIRE(true);
}
