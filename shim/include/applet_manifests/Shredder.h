#pragma once
// Vendor deps: quant headers (HS::Quantize, SetScale/GetScale via HemisphereApplet,
//              OC::scale_names_short - all in shim baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Shredder {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','d');
    static constexpr const char*   name        = "Shredder";
    static constexpr const char*   description = "Cartesian sequencer: random voltage sequences on a 4x4 grid";
    static constexpr BusParam      inputs[]    = {
        { "Clock",  BusKind::gate },
        { "Reset",  BusKind::gate },
        { "X pos",  BusKind::cv   },
        { "Y pos",  BusKind::cv   },
    };
    static constexpr BusParam      outputs[]   = {
        { "Ch 1", BusKind::cv },
        { "Ch 2", BusKind::cv },
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
