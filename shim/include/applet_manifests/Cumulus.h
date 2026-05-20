#pragma once
// Vendor deps: (none)
// Cumulus.h has no internal #includes; compiles against shim baseline only.

#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct Cumulus {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','u');
    static constexpr const char*   name        = "Cumulus";
    static constexpr const char*   description = "Clocked bit-accumulator with per-bit CV outputs.";
    static constexpr BusParam      inputs[]    = {
        { "Clock", BusKind::gate },
        { "CV",    BusKind::cv   },
    };
    static constexpr BusParam      outputs[]   = {
        { "Out A", BusKind::cv },
        { "Out B", BusKind::cv },
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
