#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct ShiftGate {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','g');
    static constexpr const char*   name        = "ShiftGate";
    static constexpr const char*   description = "Shift register gate sequencer: clock shifts bits through register, outputs gate or trigger";

    // Input 0: Clock (gate). Input 1: Freeze (gate). Input 2: Flip0 CV (cv). Input 3: Flip1 CV (cv).
    static constexpr BusParam inputs[] = {
        {"Clock",   BusKind::gate},
        {"Freeze",  BusKind::gate},
        {"Flip0 CV", BusKind::cv},
        {"Flip1 CV", BusKind::cv},
    };

    // Output 0: gate/trigger for register 0. Output 1: gate/trigger for register 1.
    static constexpr BusParam outputs[] = {
        {"Out 1", BusKind::gate},
        {"Out 2", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
