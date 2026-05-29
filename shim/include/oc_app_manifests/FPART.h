#pragma once
// Vendor app: APP_FPART.h ("4 Parts": a 4-voice chord-step sequencer with a
// staff-like display, by Jesse Dinneen).
//
// O_C-app manifest for FPART. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/Low_rents.h): it declares the fixed 12-row I/O
// routing block the per-app runtime emits (oc_runtime::emit_io_params) as four
// CV inputs, four CV outputs, and four trigger inputs. These BusParam names are
// documentation only; the runtime emits its own generic row names ("CV in 1"
// etc.). The names below document the vendor app's wiring:
//
//   CV in 1..2 map to FPART_isr's two ADC reads (APP_FPART.h:591,600): CV1
//   jumps to an absolute chord (0..98), CV2 selects within the active loop.
//   CV in 3..4 are unused by the vendor app.
//   CV out A..D carry the four voices' 1V/oct pitch (FPART_isr set_pitch on
//   DAC_CHANNEL_A..D, APP_FPART.h:415-418).
//   TR in 1..4 map to FPART_isr's four DigitalInput branches
//   (APP_FPART.h:566-587): step back, step forward, jump to loop start, jump to
//   loop end.
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh). Shipped OC guids: OCLR, OCHA,
// OCSb; OCFP is unique.
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct FPART {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'F', 'P');
    static constexpr const char* name        = "4 Parts";
    static constexpr const char* description = "4-voice chord-step sequencer with staff display (O_C APP_FPART port)";

    static constexpr BusParam inputs[] = {
        {"Chord sel CV", BusKind::cv}, {"Loop sel CV", BusKind::cv},
        {"CV in 3", BusKind::cv},      {"CV in 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Voice A", BusKind::cv}, {"Voice B", BusKind::cv},
        {"Voice C", BusKind::cv}, {"Voice D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Step back", BusKind::gate},      {"Step forward", BusKind::gate},
        {"To loop start", BusKind::gate},  {"To loop end", BusKind::gate},
    };
};
}  // namespace oc_app
