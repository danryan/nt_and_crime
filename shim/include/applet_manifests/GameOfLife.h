#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct GameOfLife {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','G','l');
    static constexpr const char*   name        = "GameOfLife";
    static constexpr const char*   description = "Conway's Game of Life cellular automaton";
    static constexpr BusParam      inputs[]    = {
        {"Clock",  BusKind::gate},
        {"Draw",   BusKind::gate},
        {"X pos",  BusKind::cv},
        {"Y pos",  BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Global", BusKind::cv},
        {"Local",  BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
