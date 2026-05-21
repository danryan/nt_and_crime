#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct ClkToGate {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','G');
    static constexpr const char*   name        = "Clk2Gate";
    static constexpr const char*   description = "Clock-to-gate: converts clock inputs to variable-width gate pulses";
    static constexpr BusParam      inputs[]    = {{"Clk1", BusKind::gate}, {"Clk2", BusKind::gate},
                                                  {"PW1",  BusKind::cv},   {"PW2",  BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Gate1", BusKind::gate}, {"Gate2", BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
