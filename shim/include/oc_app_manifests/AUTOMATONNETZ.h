#pragma once
// Vendor app: APP_AUTOMATONNETZ.h (Automatonnetz: a vector-sequencer that walks
// a 5x5 grid of tonnetz transform cells and outputs a neo-Riemannian triad as
// 1V/oct pitch).
//
// O_C-app manifest for Automatonnetz. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/Harrington1200.h): it declares the fixed
// 12-row I/O routing block the per-app runtime emits (oc_runtime::emit_io_params)
// as four CV inputs, four CV outputs, and four trigger inputs. The names document
// the vendor app's wiring:
//
//   CV out A carries the chord root (or the arpeggiated voice in OUTPUTA_MODE_ARP);
//   CV out B..D carry the three triad voices (APP_AUTOMATONNETZ.h:502-504).
//   TR in 1 is the grid clock (TRIGGER_MASK_GRID = DIGITAL_INPUT_1), TR in 2 the
//   arp clock (TRIGGER_MASK_ARP = DIGITAL_INPUT_2), TR in 3 the reset (held with
//   a grid clock, APP_AUTOMATONNETZ.h:396), TR in 4 a transform hold/gate
//   (read_immediate<DIGITAL_INPUT_4>, APP_AUTOMATONNETZ.h:441).
//   The four CV inputs are the spare modulation sources.
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct AUTOMATONNETZ {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'A', 'N');
    static constexpr const char* name        = "Automatonnetz";
    static constexpr const char* description = "Tonnetz vector-sequencer: a 5x5 grid of transform cells walked per clock (O_C APP_AUTOMATONNETZ port)";

    static constexpr BusParam inputs[] = {
        {"CV 1", BusKind::cv}, {"CV 2", BusKind::cv},
        {"CV 3", BusKind::cv}, {"CV 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Root out", BusKind::cv}, {"Triad 1", BusKind::cv},
        {"Triad 2", BusKind::cv}, {"Triad 3", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Grid clock", BusKind::gate}, {"Arp clock", BusKind::gate},
        {"Reset", BusKind::gate}, {"Hold", BusKind::gate},
    };
};
}  // namespace oc_app
