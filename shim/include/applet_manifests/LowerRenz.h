#pragma once
// Vendor deps: streams_lorenz_generator.h, HSLorenzGeneratorManager.h
//              links streams_resources.o + streams_lorenz_generator.o
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct LowerRenz {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','L','r');
    static constexpr const char*   name        = "LowerRenz";
    static constexpr const char*   description = "Lorenz attractor: X/Y chaotic CV outputs";
    static constexpr BusParam      inputs[]    = {
        {"Reset", BusKind::gate},
        {"Freeze", BusKind::gate},
        {"Freq",   BusKind::cv},
        {"Rho",    BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"X", BusKind::cv},
        {"Y", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
