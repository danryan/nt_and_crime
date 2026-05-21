#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct ClockSkip {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','s');
    static constexpr const char*   name        = "Clock Skip";
    static constexpr const char*   description = "Probabilistic clock skipper: passes clock with configurable probability";
    static constexpr BusParam      inputs[]    = {{"Clock 1", BusKind::gate}, {"Clock 2", BusKind::gate},
                                                   {"p CV 1",  BusKind::cv},   {"p CV 2",  BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out 1", BusKind::gate}, {"Out 2", BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
