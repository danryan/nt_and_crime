#pragma once
// Vendor app: APP_BYTEBEATGEN.h ("Byte Beats": four peaks bytebeat generators, by
// Tim Churches). Quad-channel OC::App port, sibling of BBGEN.
//
// O_C-app manifest. The per-app runtime emits a fixed 12-row I/O routing block
// (oc_runtime::emit_io_params). These BusParam names document the vendor wiring;
// the runtime emits its own generic row names ("CV in 1" etc.):
//   CV in 1..4 -> the four ADC channels the ISR smooths (APP_BYTEBEATGEN.h:418-421),
//     each routable to a bytebeat parameter via the per-channel CV1..CV4 mapping.
//   CV out A..D -> the four bytebeat samples (DAC_CHANNEL_A..D, APP_BYTEBEATGEN.h:427-430).
//   TR in 1..4 -> DIGITAL_INPUT_1..4; each channel's "Trigger input" setting picks
//     which one gates it (APP_BYTEBEATGEN.h:303-305).
//
// guid uses the "OC" prefix (shipped: OCLR, OCHA, OCSb, OCFP, OCBB); OCBT is unique.
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct BYTEBEATGEN {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'B', 'T');
    static constexpr const char* name        = "Byte Beats";
    static constexpr const char* description = "Four bytebeat generators (O_C APP_BYTEBEATGEN port)";

    static constexpr BusParam inputs[] = {
        {"CV in 1", BusKind::cv}, {"CV in 2", BusKind::cv},
        {"CV in 3", BusKind::cv}, {"CV in 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Beat A", BusKind::cv}, {"Beat B", BusKind::cv},
        {"Beat C", BusKind::cv}, {"Beat D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Trig 1", BusKind::gate}, {"Trig 2", BusKind::gate},
        {"Trig 3", BusKind::gate}, {"Trig 4", BusKind::gate},
    };
};
}  // namespace oc_app
