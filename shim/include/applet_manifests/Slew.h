#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Slew {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','l');
    static constexpr const char*   name        = "Slew";
    static constexpr const char*   description = "Slew limiter: smooths CV signals with configurable rise/fall times";
    static constexpr BusParam      inputs[]    = {{"Defeat 1", BusKind::gate}, {"Defeat 2", BusKind::gate},
                                                  {"CV 1", BusKind::cv},       {"CV 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out 1", BusKind::cv}, {"Out 2", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
