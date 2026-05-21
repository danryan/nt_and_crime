#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Trending {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','T','r');
    static constexpr const char*   name        = "Trending";
    static constexpr const char*   description = "Detects signal trend: outputs gate/clock on rising, falling, moving, or steady";
    static constexpr BusParam      inputs[]    = {{"Sig 1", BusKind::cv}, {"Sig 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out 1", BusKind::gate}, {"Out 2", BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
