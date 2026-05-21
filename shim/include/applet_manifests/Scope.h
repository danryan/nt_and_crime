#pragma once
// Vendor deps: OC::CORE::ticks (shim baseline, no extra includes needed)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Scope {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','o');
    static constexpr const char*   name        = "Scope";
    static constexpr const char*   description = "Dual-channel oscilloscope with BPM meter";
    static constexpr BusParam      inputs[]    = {
        {"BPM Clk", BusKind::gate},
        {"Cycle",   BusKind::gate},
        {"CV 1",    BusKind::cv},
        {"CV 2",    BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"CV 1", BusKind::cv},
        {"CV 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
