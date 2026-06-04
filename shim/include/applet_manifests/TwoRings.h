#pragma once
// Vendor deps: (none) -- QuantizerLookup is shim baseline
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct TwoRings {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','2','R');
    static constexpr const char* name        = "TwoRings";
    static constexpr const char* description = "Dual Turing machine shift-register sequencer.";

    static constexpr BusParam inputs[] = {
        {"Clock",  BusKind::gate},
        {"p Gate", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Out A", BusKind::cv},
        {"Out B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
