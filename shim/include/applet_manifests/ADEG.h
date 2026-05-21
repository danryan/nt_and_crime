#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct ADEG {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','A','D');
    static constexpr const char*   name        = "AD EG";
    static constexpr const char*   description = "AD envelope generator with reverse trigger";
    static constexpr BusParam      inputs[]    = {{"Trigger", BusKind::gate}, {"Reverse", BusKind::gate},
                                                   {"Attack",  BusKind::cv},  {"Decay",   BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out", BusKind::cv}, {"EOC", BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
