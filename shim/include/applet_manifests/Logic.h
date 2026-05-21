#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Logic {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','L','g');
    static constexpr const char*   name        = "Logic";
    static constexpr const char*   description = "Logic: dual logic gate (AND/OR/XOR/NAND/NOR/XNOR) on gate inputs";
    static constexpr BusParam      inputs[]    = {{"Gate 1", BusKind::gate}, {"Gate 2", BusKind::gate}};
    static constexpr BusParam      outputs[]   = {{"Out 1", BusKind::gate}, {"Out 2", BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
