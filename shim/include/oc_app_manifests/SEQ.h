#pragma once
// Vendor app: APP_SEQ.h (Sequins: a dual-channel step sequencer with per-channel
// scale quantizing, an arpeggiator, and an aux multistage envelope).
//
// O_C-app manifest for Sequins. Mirrors the per-app manifest shape: the fixed
// 12-row I/O routing block the per-app runtime emits (oc_runtime::emit_io_params)
// as four CV inputs, four CV outputs, and four trigger inputs. The vendor wiring
// (APP_SEQ.h SEQ_isr):
//
//   seq_channel[0] -> DAC_CHANNEL_A (main pitch) + DAC_CHANNEL_C (aux env),
//   seq_channel[1] -> DAC_CHANNEL_B (main pitch) + DAC_CHANNEL_D (aux env). The
//   runtime CV-out order is A, B, C, D, so out 1/2 are the two channels' main
//   pitch and out 3/4 their aux outputs.
//   The two clocks are DIGITAL_INPUT_1 (TR1, channel 0) and DIGITAL_INPUT_3 (TR3,
//   channel 1); reset/mute and the rich CV-mapping page read the trigger and CV
//   inputs, themselves settings.
//
// I/O shape is identical to the Dual Quantizer (DQ): two channels, main + aux per
// channel, clocked on TR1 / TR3. The guid uses the "OC" prefix so it never
// collides with the Hemisphere "Hm" space or the composer host guids.
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct SEQ {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'S', 'Q');
    static constexpr const char* name        = "Sequins";
    static constexpr const char* description = "Dual step sequencer, per-channel quantizing + arp + aux envelope (O_C APP_SEQ / Sequins port)";

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
