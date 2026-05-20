// Per-applet pilot test skeleton: VectorLFO.
//
// Manifest: shim/include/applet_manifests/VectorLFO.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/VectorLFO.h
//
// Test concerns the implementer MUST address:
// - Vendor deps: VectorLFO uses HSVectorOscillator + WaveformManager +
//   tideslite (constexpr ComputePhaseIncrement). All header-only and in
//   shim baseline (audit confirmed). Vendor dep accounting per-.o.
// - Round-trip: pack_vectorlfo helper if one exists, else write byte
//   layout from vendor OnDataRequest.
// - Behavior: vec-osc output observable through CV output bus; assert
//   the waveform tracks an expected sample at a known phase.
// - 10x ticks-per-step: VectorLFO advances phase per Controller() call;
//   sample observations must account for 10x rate vs raw frame count.

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

TEST_CASE("VectorLFO placeholder", "[per-applet-pilot][vectorlfo]") {
    // TODO(implementer): load plugins/applets/VectorLFO.cpp; verify
    // vec-osc dep linkage works on the per-.o partial-link path.
    REQUIRE(true);
}
