#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Voltage {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','V','o');
    static constexpr const char*   name        = "Voltage";
    static constexpr const char*   description = "Dual CV source with gate-controlled output";
    static constexpr BusParam      inputs[]    = {{"Gate 1", BusKind::gate}, {"Gate 2", BusKind::gate}};
    static constexpr BusParam      outputs[]   = {{"Volt 1", BusKind::cv}, {"Volt 2", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
