#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Brancher {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','B','r');
    static constexpr const char*   name        = "Brancher";
    static constexpr const char*   description = "Probabilistic gate router: routes clock/gate to one of two outputs";

    // Input 0: Clock/Gate (gate). Input 1: AltClk (gate). Input 2: p Mod (cv).
    static constexpr BusParam inputs[] = {
        {"Clock/Gate", BusKind::gate},
        {"AltClk",     BusKind::gate},
        {"p Mod",      BusKind::cv},
    };

    // Output 0: Left gate. Output 1: Right gate.
    static constexpr BusParam outputs[] = {
        {"Left",  BusKind::gate},
        {"Right", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
