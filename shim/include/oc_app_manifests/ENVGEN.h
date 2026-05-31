#pragma once
// Vendor app: APP_ENVGEN.h ("Piqued" / "4x EG": four independent
// peaks::MultistageEnvelope envelope generators with per-channel euclidean
// gating and a trigger-delay queue, by Patrick Dowling).
//
// O_C-app manifest for ENVGEN. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/BBGEN.h, the quad-channel template): the fixed
// 12-row I/O routing block the per-app runtime emits (oc_runtime::emit_io_params)
// as four CV inputs, four CV outputs, and four trigger inputs. These BusParam
// names are documentation only; the runtime emits its own generic row names.
//
//   CV in 1..4 map to the four ADC reads in QuadEnvelopeGenerator::ISR
//   (APP_ENVGEN.h:803-812), each routable to any channel's segment/time/amplitude
//   via that channel's CV1..CV4 mapping settings.
//   CV out A..D carry the four envelopes' unipolar modulation output
//   (envelopes_[i].Update(... DAC_CHANNEL_A..D), APP_ENVGEN.h:818-821).
//   TR in 1..4 are the four channels' default gate/trigger inputs (each
//   envelope Init()s with DIGITAL_INPUT_1..4, APP_ENVGEN.h:786-789).
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids. Shipped OC guids: OCLR, OCHA, OCSb, OCFP,
// OCBB, OCBT, OCPL; OCEG is unique (matches the vendor "EG" two-char id).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct ENVGEN {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'E', 'G');
    static constexpr const char* name        = "4x EG";
    static constexpr const char* description = "Four multistage envelope generators with euclidean gating (O_C APP_ENVGEN port)";

    static constexpr BusParam inputs[] = {
        {"CV in 1", BusKind::cv}, {"CV in 2", BusKind::cv},
        {"CV in 3", BusKind::cv}, {"CV in 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Env A", BusKind::cv}, {"Env B", BusKind::cv},
        {"Env C", BusKind::cv}, {"Env D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Trig A", BusKind::gate}, {"Trig B", BusKind::gate},
        {"Trig C", BusKind::gate}, {"Trig D", BusKind::gate},
    };
};
}  // namespace oc_app
