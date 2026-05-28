#pragma once
// Vendor app: APP_H1200.h (Harrington 1200 neo-Riemannian tonnetz triad
// transformer).
//
// O_C-app manifest for Harrington 1200. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/Low_rents.h): it declares the fixed 12-row I/O
// routing block the per-app runtime emits (oc_runtime::emit_io_params) as four
// CV inputs, four CV outputs, and four trigger inputs. The names document the
// vendor app's wiring:
//
//   CV in 1 carries the root/transpose pitch CV (the app's CV sources map any
//   of CV1..4 to root/octave/inversion/transform priority; the default
//   wiring sources from CV1). CV in 4 carries the inversion CV in the common
//   patch. CV2/CV3 are the spare CV sources.
//   CV out A..D carry the rendered triad: out A is the chord root, out B..D the
//   three triad voices (APP_H1200.h:631-634).
//   TR in 1..4 are reset, P, L, R transform triggers in PLR mode (the masks at
//   APP_H1200.h:482-485: TR1 reset, TR2 P/N, TR3 L/S, TR4 R/H).
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh), and the "HA" suffix is
// distinct from Low-rents (OCLR) and the stub (OCSb).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct Harrington1200 {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'H', 'A');
    static constexpr const char* name        = "Harrington 1200";
    static constexpr const char* description = "Neo-Riemannian tonnetz triad transformer (O_C APP_H1200 port)";

    static constexpr BusParam inputs[] = {
        {"Root CV", BusKind::cv}, {"CV 2", BusKind::cv},
        {"CV 3", BusKind::cv}, {"Inversion CV", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Root out", BusKind::cv}, {"Triad 1", BusKind::cv},
        {"Triad 2", BusKind::cv}, {"Triad 3", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Reset", BusKind::gate}, {"P / N", BusKind::gate},
        {"L / S", BusKind::gate}, {"R / H", BusKind::gate},
    };
};
}  // namespace oc_app
