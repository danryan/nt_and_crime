#pragma once
// Vendor app: APP_ASR.h (Analog Shift Register: a clocked four-stage quantizing
// shift register; each clock shifts the quantized input down the four DAC
// outputs).
//
// O_C-app manifest for the ASR. Mirrors the per-app manifest shape: the fixed
// 12-row I/O routing block the per-app runtime emits (oc_runtime::emit_io_params)
// as four CV inputs, four CV outputs, and four trigger inputs. The vendor wiring
// (APP_ASR.h:721):
//
//   ASR_isr writes OC::DAC::set((DAC_CHANNEL)i, outputs[i].get()) for i in 0..3,
//   so the four CV outputs are the four shift-register stages (oldest to newest
//   sample). The CV inputs feed the quantizer source (CV1 default), the slew CV,
//   and the CV4 destination modulation; the trigger inputs clock the register.
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct ASR {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'A', 'S');
    static constexpr const char* name        = "Analog Shift Reg";
    static constexpr const char* description = "Clocked four-stage quantizing shift register with Turing / bytebeat / int-seq sources (O_C APP_ASR port)";

    static constexpr BusParam inputs[] = {
        {"CV 1", BusKind::cv}, {"CV 2", BusKind::cv},
        {"CV 3", BusKind::cv}, {"CV 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Stage 1", BusKind::cv}, {"Stage 2", BusKind::cv},
        {"Stage 3", BusKind::cv}, {"Stage 4", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Clock", BusKind::gate}, {"TR 2", BusKind::gate},
        {"TR 3", BusKind::gate}, {"TR 4", BusKind::gate},
    };
};
}  // namespace oc_app
