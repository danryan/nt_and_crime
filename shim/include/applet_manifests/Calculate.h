#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Calculate {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','a');
    static constexpr const char*   name        = "Calculate";
    static constexpr const char*   description = "Calculate: arithmetic and S&H on two CV inputs";
    static constexpr BusParam      inputs[]    = {
        {"Gate A", BusKind::gate},
        {"Gate B", BusKind::gate},
        {"CV 1",   BusKind::cv},
        {"CV 2",   BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Out 1", BusKind::cv},
        {"Out 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
