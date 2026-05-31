#pragma once
// Vendor app: APP_DQ.h (Meta-Q / Dual Quantizer: two independent quantizer
// channels, each with four scale slots, a Turing-machine source, and a main +
// aux CV output).
//
// O_C-app manifest for the Dual Quantizer. Mirrors the per-app manifest shape:
// the fixed 12-row I/O routing block the per-app runtime emits
// (oc_runtime::emit_io_params) as four CV inputs, four CV outputs, and four
// trigger inputs. The vendor wiring (APP_DQ.h:1229-1230):
//
//   DQ_isr drives channel 0 -> DAC_CHANNEL_A (main) + DAC_CHANNEL_C (aux),
//   channel 1 -> DAC_CHANNEL_B (main) + DAC_CHANNEL_D (aux). The runtime CV-out
//   order is A, B, C, D, so out 1/2 are the two channels' main pitch and out 3/4
//   their aux outputs.
//   Each channel quantizes a CV input (channel 0 defaults to CV1, channel 1 to
//   CV3) and is clocked by a trigger (channel 0 TR1, channel 1 TR3); the source
//   and trigger are themselves settings.
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct DQ {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'D', 'Q');
    static constexpr const char* name        = "Dual Quantizer";
    static constexpr const char* description = "Two quantizer channels, four scale slots each, main + aux CV out (O_C APP_DQ / Meta-Q port)";

    static constexpr BusParam inputs[] = {
        {"CV 1", BusKind::cv}, {"CV 2", BusKind::cv},
        {"CV 3", BusKind::cv}, {"CV 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Ch1 pitch", BusKind::cv}, {"Ch2 pitch", BusKind::cv},
        {"Ch1 aux", BusKind::cv}, {"Ch2 aux", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Ch1 clock", BusKind::gate}, {"TR 2", BusKind::gate},
        {"Ch2 clock", BusKind::gate}, {"TR 4", BusKind::gate},
    };
};
}  // namespace oc_app
