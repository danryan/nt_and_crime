#pragma once
// Vendor app: APP_QQ.h ("Quantermain": four independent quantizer channels, each
// with a scale + mask, a Turing / logistic / bytebeat / integer-sequence source,
// and a 1V/oct pitch output).
//
// O_C-app manifest for Quantermain. Mirrors the per-app manifest shape: the fixed
// 12-row I/O routing block the per-app runtime emits (oc_runtime::emit_io_params)
// as four CV inputs, four CV outputs, and four trigger inputs. The vendor wiring
// (APP_QQ.h:1288-1291):
//
//   QQ_isr runs quantizer_channels[i].Update(triggers, DAC_CHANNEL_i) for i in
//   A..D, so output i is channel i's quantized 1V/oct pitch. Each channel
//   quantizes a routed CV input (the SOURCE setting) and is clocked by a trigger
//   (the TRIGGER setting); both source and trigger are themselves settings.
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct QQ {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'Q', 'Q');
    static constexpr const char* name        = "Quantermain";
    static constexpr const char* description = "Four quantizer channels with scale, mask, and Turing/logistic/bytebeat/int-seq sources (O_C APP_QQ port)";

    static constexpr BusParam inputs[] = {
        {"CV 1", BusKind::cv}, {"CV 2", BusKind::cv},
        {"CV 3", BusKind::cv}, {"CV 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Ch1 pitch", BusKind::cv}, {"Ch2 pitch", BusKind::cv},
        {"Ch3 pitch", BusKind::cv}, {"Ch4 pitch", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Ch1 clock", BusKind::gate}, {"Ch2 clock", BusKind::gate},
        {"Ch3 clock", BusKind::gate}, {"Ch4 clock", BusKind::gate},
    };
};
}  // namespace oc_app
