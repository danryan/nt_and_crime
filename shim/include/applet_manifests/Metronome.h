#pragma once
// Vendor deps: HSClockManager (global clock_m, shim baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Metronome {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','M','t');
    static constexpr const char*   name        = "Metronome";
    static constexpr const char*   description = "Metronome: tempo/swing clock source with multiply output";
    static constexpr BusParam      inputs[]    = {
        {"Clock", BusKind::gate},
        {"Tempo", BusKind::cv},
        {"Swing", BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Mult",  BusKind::gate},
        {"Run",   BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
