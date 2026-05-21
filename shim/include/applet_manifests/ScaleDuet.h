#pragma once
// Vendor deps: braids::Quantizer (quant headers, shim baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct ScaleDuet {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','D');
    static constexpr const char*   name        = "ScaleDuet";
    static constexpr const char*   description = "Scale quantizer: two masks, gate selects scale";
    static constexpr BusParam      inputs[]    = {
        {"Clock", BusKind::gate},
        {"Scale2", BusKind::gate},
        {"CV",     BusKind::cv},
        {"Unclock",BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Pitch", BusKind::cv},
        {"Trig",  BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
