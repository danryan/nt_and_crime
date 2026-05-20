#pragma once
// Vendor deps: (none) — vector_osc + tideslite headers are in shim baseline;
// tideslite.cpp is NOT linked (VectorLFO uses only constexpr ComputePhaseIncrement).
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct VectorLFO {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','V','l');
    static constexpr const char*   name        = "VectorLFO";
    static constexpr const char*   description = "Dual vector oscillator LFO.";
    static constexpr BusParam      inputs[]    = {
        {"Freq CV", BusKind::cv},
        {"Reset",   BusKind::gate},
    };
    static constexpr BusParam      outputs[]   = {
        {"Out A", BusKind::cv},
        {"Out B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
