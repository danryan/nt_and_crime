#pragma once
// Vendor app: APP_POLYLFO.h ("Poly LFO": a quadrature wavetable LFO with four
// phase-related outputs, based on the Mutable Instruments Frames easter egg, by
// Patrick Dowling and Tim Churches).
//
// O_C-app manifest for POLYLFO. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/FPART.h): it declares the fixed 12-row I/O
// routing block the per-app runtime emits (oc_runtime::emit_io_params) as four
// CV inputs, four CV outputs, and four trigger inputs. These BusParam names are
// documentation only; the runtime emits its own generic row names ("CV in 1"
// etc.). The names below document the vendor app's wiring:
//
//   CV in 1..4 map to POLYLFO_isr's four ADC reads (APP_POLYLFO.h:286-289):
//   frequency, shape, spread, and the mappable CV (whose destination the CV4
//   setting selects).
//   CV out A..D carry the four quadrature LFO voices' full-scale modulation
//   (POLYLFO_isr DAC writes, APP_POLYLFO.h:380-383).
//   TR in 1..3 map to POLYLFO_isr's three DigitalInput reads
//   (APP_POLYLFO.h:276-278): reset phase, freeze, tempo sync. TR in 4 is the
//   free-run frequency-multiplier override (digitalReadFast(TR4),
//   APP_POLYLFO.h:374).
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh). Shipped OC guids: OCLR, OCHA,
// OCSb, OCFP, OCBB, OCBT; OCPL is unique.
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct POLYLFO {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'P', 'L');
    static constexpr const char* name        = "Poly LFO";
    static constexpr const char* description = "Quadrature wavetable poly LFO, 4 phase-related outputs (O_C APP_POLYLFO port)";

    static constexpr BusParam inputs[] = {
        {"Freq CV", BusKind::cv},   {"Shape CV", BusKind::cv},
        {"Spread CV", BusKind::cv}, {"Mappable CV", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"LFO A", BusKind::cv}, {"LFO B", BusKind::cv},
        {"LFO C", BusKind::cv}, {"LFO D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Reset phase", BusKind::gate}, {"Freeze", BusKind::gate},
        {"Tempo sync", BusKind::gate},  {"Freq mult", BusKind::gate},
    };
};
}  // namespace oc_app
