#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Shuffle {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','h');
    static constexpr const char*   name        = "Shuffle";
    static constexpr const char*   description = "Swing/shuffle delay on clock with triplets output";

    // Input 0: Clock (gate). Input 1: Reset (gate).
    // Input 2: Odd delay CV (cv). Input 3: Even delay CV (cv).
    static constexpr BusParam inputs[] = {
        {"Clock",    BusKind::gate},
        {"Reset",    BusKind::gate},
        {"Odd Mod",  BusKind::cv},
        {"Even",     BusKind::cv},
    };

    // Output 0: Shuffle (delayed clock). Output 1: Triplets (3/4 of clock rate).
    static constexpr BusParam outputs[] = {
        {"Shuffle",  BusKind::gate},
        {"Triplets", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
