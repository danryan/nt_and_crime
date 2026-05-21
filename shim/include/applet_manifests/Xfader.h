#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Xfader {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','X','f');
    static constexpr const char*   name        = "Xfader";
    static constexpr const char*   description = "Crossfader: mixes two CV signals with gate-driven fade and center reset";

    // Input 0: Gate ch 0 "Fade L" (fades balance left). Input 1: Gate ch 1 "Fade R".
    // Input 2: CV ch 0 "Sig 1". Input 3: CV ch 1 "Sig 2".
    static constexpr BusParam inputs[] = {
        {"Fade L", BusKind::gate},
        {"Fade R", BusKind::gate},
        {"Sig 1",  BusKind::cv},
        {"Sig 2",  BusKind::cv},
    };

    // Output 0: CV "Mix 1+2". Output 1: CV "Mix 2+1".
    static constexpr BusParam outputs[] = {
        {"Mix 1+2", BusKind::cv},
        {"Mix 2+1", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
