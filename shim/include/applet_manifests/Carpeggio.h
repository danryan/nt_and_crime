#pragma once
// Vendor deps: hem_arp_chord.h (standalone data header, shim baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Carpeggio {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','g');
    static constexpr const char*   name        = "Carpeggio";
    static constexpr const char*   description = "Cartesian arpeggiator over a 4x4 chord grid";
    static constexpr BusParam      inputs[]    = {
        {"Clock",  BusKind::gate},
        {"Reset",  BusKind::gate},
    };
    static constexpr BusParam      outputs[]   = {
        {"Pitch",  BusKind::cv},
        {"Mod",    BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
