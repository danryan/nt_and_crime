#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Switch {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','w');
    static constexpr const char*   name        = "Switch";
    static constexpr const char*   description = "Sequential and gated CV switch";
    static constexpr BusParam      inputs[]    = {
        {"Clock", BusKind::gate},
        {"Gate",  BusKind::gate},
        {"CV 1",  BusKind::cv},
        {"CV 2",  BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Toggled", BusKind::cv},
        {"Gated",   BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
