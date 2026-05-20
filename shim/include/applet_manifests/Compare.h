#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Compare {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','p');
    static constexpr const char*   name        = "Compare";
    static constexpr const char*   description = "Comparator: drives gate high when CV1 > CV2";
    static constexpr BusParam      inputs[]    = {{"CV 1", BusKind::cv}, {"CV 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"GT", BusKind::gate}, {"Min", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
