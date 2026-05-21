#pragma once
// Vendor deps: HS::Quantize() on shim base class (already in baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct DualQuant {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','D','q');
    static constexpr const char*   name        = "DualQuant";
    static constexpr const char*   description = "Dual quantizer with independent scales and roots";
    static constexpr BusParam      inputs[]    = {
        {"Clock 1", BusKind::gate},
        {"Clock 2", BusKind::gate},
        {"CV 1",    BusKind::cv},
        {"CV 2",    BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Pitch 1", BusKind::cv},
        {"Pitch 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
