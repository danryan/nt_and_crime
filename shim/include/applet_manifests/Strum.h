#pragma once
// Vendor deps: quant headers (HS::Quantize, shim baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Strum {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','r');
    static constexpr const char*   name        = "Strum";
    static constexpr const char*   description = "Strum a chord upward or downward from a root pitch";
    static constexpr BusParam      inputs[]    = {{"Strum Up",  BusKind::gate},
                                                   {"Strum Dn",  BusKind::gate},
                                                   {"Root",      BusKind::cv},
                                                   {"Spacing",   BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Pitch",  BusKind::cv},
                                                   {"Trig",   BusKind::gate}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
