#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Stairs {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','t');
    static constexpr const char*   name        = "Stairs";
    static constexpr const char*   description = "Clocked staircase CV generator with up/down/bounce modes";

    // Input 0: Clock (gate). Input 1: Reset (gate). Input 2: Steps CV (cv). Input 3: Position CV (cv).
    static constexpr BusParam inputs[] = {
        {"Clock",    BusKind::gate},
        {"Reset",    BusKind::gate},
        {"Steps CV", BusKind::cv},
        {"Pos CV",   BusKind::cv},
    };

    // Output 0: Step CV (cv). Output 1: BoC Trigger (gate).
    static constexpr BusParam outputs[] = {
        {"Step CV",  BusKind::cv},
        {"BoC Trg",  BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
