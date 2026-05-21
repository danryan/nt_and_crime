#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Button {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','B','t');
    static constexpr const char*   name        = "Button";
    static constexpr const char*   description = "Button: manual trigger/gate with two configurable outputs";
    static constexpr BusParam      inputs[]    = {{"Trig 1", BusKind::gate}, {"Trig 2", BusKind::gate}};
    static constexpr BusParam      outputs[]   = {{"Out A", BusKind::gate}, {"Out B", BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
