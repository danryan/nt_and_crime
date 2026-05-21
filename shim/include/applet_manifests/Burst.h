#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Burst {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','B','u');
    static constexpr const char*   name        = "Burst";
    static constexpr const char*   description = "Burst generator: fires N clocked pulses on each trigger.";
    static constexpr BusParam      inputs[]    = {
        { "Clock",   BusKind::gate },
        { "Trigger", BusKind::gate },
        { "Number",  BusKind::cv   },
        { "Spacing", BusKind::cv   },
    };
    static constexpr BusParam      outputs[]   = {
        { "Burst", BusKind::gate },
        { "Gate",  BusKind::gate },
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
