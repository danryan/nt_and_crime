#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct RunglBook {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','R','g');
    static constexpr const char*   name        = "RunglBook";
    static constexpr const char*   description = "Shift register sequencer: clocks CV into 8-bit reg, outputs rungle voltages";

    // Digital 1: Clock (gate). Digital 2: Freeze (gate).
    // CV 1: Signal (cv). CV 2: Threshold mod (cv).
    static constexpr BusParam inputs[] = {
        {"Clock",  BusKind::gate},
        {"Freeze", BusKind::gate},
        {"Signal", BusKind::cv},
        {"Thresh", BusKind::cv},
    };

    // Out 1: Rungle (lower 3 bits). Out 2: Alt (upper 3 bits shifted).
    static constexpr BusParam outputs[] = {
        {"Rungle", BusKind::cv},
        {"Alt",    BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
