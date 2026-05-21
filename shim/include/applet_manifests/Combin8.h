#pragma once
// Vendor deps: CVInputMap (via HemisphereApplet base class; no extra Makefile deps)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Combin8 {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','n');
    static constexpr const char*   name        = "Combin8";
    static constexpr const char*   description = "3-input CV combiner: sums main input plus two aux CVInputMap sources per channel";
    static constexpr BusParam      inputs[]    = {{"CV 1", BusKind::cv}, {"CV 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out 1", BusKind::cv}, {"Out 2", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
