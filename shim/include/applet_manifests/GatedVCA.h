#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct GatedVCA {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','G','v');
    static constexpr const char*   name        = "GatedVCA";
    static constexpr const char*   description = "GatedVCA: gated VCA with normally-on and normally-off outputs";
    static constexpr BusParam      inputs[]    = {
        {"Gate 1", BusKind::gate},
        {"Mute 2", BusKind::gate},
        {"Signal", BusKind::cv},
        {"Amp",    BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Closed", BusKind::cv},
        {"Open",   BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
