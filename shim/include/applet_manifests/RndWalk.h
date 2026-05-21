#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct RndWalk {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','R','w');
    static constexpr const char*   name        = "RndWalk";
    static constexpr const char*   description = "Dual random walk CV generator clocked by TR1/TR2.";

    static constexpr BusParam inputs[] = {
        {"X Clk", BusKind::gate},
        {"Y Clk", BusKind::gate},
        {"Range", BusKind::cv},
        {"Step",  BusKind::cv},
    };

    static constexpr BusParam outputs[] = {
        {"X", BusKind::cv},
        {"Y", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
