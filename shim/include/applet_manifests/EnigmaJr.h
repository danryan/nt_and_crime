#pragma once
// Vendor deps: enigma/TuringMachine.h, enigma/TuringMachineState.h,
// enigma/EnigmaOutput.h (pulls braids_quantizer, OC_scales transitively).
// HSMIDI.h shim stubs satisfy usbMIDI calls in EnigmaOutput.h.
// user_turing_machines[40] is a shared singleton by vendor design.

#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct EnigmaJr {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','E','j');
    static constexpr const char*   name        = "EnigmaJr";
    static constexpr const char*   description = "Turing-machine sequencer with configurable note/mod/gate outputs";
    static constexpr BusParam      inputs[]    = {
        {"Clock",   BusKind::gate},
        {"Reset",   BusKind::gate},
        {"Shift",   BusKind::cv},
        {"Organize", BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Out A", BusKind::cv},
        {"Out B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
