#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Schmitt {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','c');
    static constexpr const char*   name        = "Schmitt";
    static constexpr const char*   description = "Schmitt trigger: hysteresis window converts CV to gate";
    static constexpr BusParam      inputs[]    = {{"CV 1", BusKind::cv}, {"CV 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Gate 1", BusKind::gate}, {"Gate 2", BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
