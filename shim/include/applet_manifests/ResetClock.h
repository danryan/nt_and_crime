#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct ResetClock {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','R','c');
    static constexpr const char*   name        = "ResetClock";
    static constexpr const char*   description = "Clock that resets to a position after N steps";
    static constexpr BusParam      inputs[]    = {
        {"Clock",  BusKind::gate},
        {"Reset",  BusKind::gate},
        {"Offset", BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Advance", BusKind::gate},
        {"Trigger", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
