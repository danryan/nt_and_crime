#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct ADSREG {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','A','d');
    static constexpr const char*   name        = "ADSR EG";
    static constexpr const char*   description = "Dual ADSR envelope generator; CV inputs modulate release";
    static constexpr BusParam      inputs[]    = {
        {"Gate 1",   BusKind::gate},
        {"Gate 2",   BusKind::gate},
        {"Release1", BusKind::cv},
        {"Release2", BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Amp 1", BusKind::cv},
        {"Amp 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
