#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// PolyDiv: 4 independent clock dividers routed to 2 outputs via enable matrix.
// Clock input (digital 0) advances all four dividers; reset input (digital 1)
// resets all clock_count fields. CV inputs 0 and 1 gate XOR mode per output.

namespace per_applet {

struct PolyDiv {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','P','o');
    static constexpr const char* name        = "PolyDiv";
    static constexpr const char* description = "4 clock dividers routed to 2 trigger outputs.";
    static constexpr BusParam    inputs[]    = {
        { "Clock", BusKind::gate },
        { "Reset", BusKind::gate },
    };
    static constexpr BusParam    outputs[]   = {
        { "Out A", BusKind::gate },
        { "Out B", BusKind::gate },
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
