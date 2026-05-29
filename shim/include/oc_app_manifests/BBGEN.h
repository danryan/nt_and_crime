#pragma once
// Vendor app: APP_BBGEN.h ("Bouncing Balls": four peaks bouncing-ball envelope
// generators, by Tim Churches). First quad-channel OC::App port.
//
// O_C-app manifest. The per-app runtime emits a fixed 12-row I/O routing block
// (oc_runtime::emit_io_params). These BusParam names document the vendor wiring;
// the runtime emits its own generic row names ("CV in 1" etc.):
//   CV in 1..4 -> the four ADC channels the ISR smooths (APP_BBGEN.h:250-253),
//     each routable to a ball parameter via the per-ball CV1..CV4 mapping.
//   CV out A..D -> the four ball envelopes (DAC_CHANNEL_A..D, APP_BBGEN.h:259).
//   TR in 1..4 -> DIGITAL_INPUT_1..4; each ball's "Trigger input" setting picks
//     which one gates it (APP_BBGEN.h:171-181).
//
// guid uses the "OC" prefix (shipped: OCLR, OCHA, OCSb, OCFP); OCBB is unique.
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct BBGEN {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'B', 'B');
    static constexpr const char* name        = "Bouncing Balls";
    static constexpr const char* description = "Four bouncing-ball envelope generators (O_C APP_BBGEN port)";

    static constexpr BusParam inputs[] = {
        {"CV in 1", BusKind::cv}, {"CV in 2", BusKind::cv},
        {"CV in 3", BusKind::cv}, {"CV in 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Ball A", BusKind::cv}, {"Ball B", BusKind::cv},
        {"Ball C", BusKind::cv}, {"Ball D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Trig 1", BusKind::gate}, {"Trig 2", BusKind::gate},
        {"Trig 3", BusKind::gate}, {"Trig 4", BusKind::gate},
    };
};
}  // namespace oc_app
