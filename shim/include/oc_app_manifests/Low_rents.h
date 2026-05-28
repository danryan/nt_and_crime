#pragma once
// Vendor app: APP_LORENZ.h (Lorenz and Rossler chaotic generators).
//
// O_C-app manifest for Low-rents. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/StubApp.h): it declares the fixed 12-row I/O
// routing block the per-app runtime emits (oc_runtime::emit_io_params) as four
// CV inputs, four CV outputs, and four trigger inputs. The names document the
// vendor app's wiring:
//
//   CV in 1..4 map, in vendor isr order (APP_LORENZ.h:191-194), to the freq and
//   rho CV of each generator: freq1, rho1, freq2, rho2.
//   CV out A..D carry the four mappable Lorenz/Rossler outputs (defaults
//   X1/Y1/X2/Y2; 22 selectable mixes via the OUT_A..D settings).
//   TR in 1..4 are reset1, reset2, reset-both, and freeze
//   (APP_LORENZ.h:180-183).
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct Low_rents {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'L', 'R');
    static constexpr const char* name        = "Low-rents";
    static constexpr const char* description = "Lorenz and Rossler chaotic generators (O_C APP_LORENZ port)";

    static constexpr BusParam inputs[] = {
        {"Freq 1 CV", BusKind::cv}, {"Rho 1 CV", BusKind::cv},
        {"Freq 2 CV", BusKind::cv}, {"Rho 2 CV", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Out A", BusKind::cv}, {"Out B", BusKind::cv},
        {"Out C", BusKind::cv}, {"Out D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Reset 1", BusKind::gate}, {"Reset 2", BusKind::gate},
        {"Reset both", BusKind::gate}, {"Freeze", BusKind::gate},
    };
};
}  // namespace oc_app
