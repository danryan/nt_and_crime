#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct GateDelay {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','G','d');
    static constexpr const char*   name        = "GateDelay";
    static constexpr const char*   description = "Delay two gate signals independently by up to 2 seconds";
    static constexpr BusParam      inputs[]    = {
        {"Gate 1", BusKind::gate},
        {"Gate 2", BusKind::gate},
        {"Time 1", BusKind::cv},
        {"Time 2", BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Delay 1", BusKind::gate},
        {"Delay 2", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
