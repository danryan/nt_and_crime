#pragma once
// Vendor deps: vec_osc headers (HSVectorOscillator.h + WaveformManager.h,
// no .cpp link required). Mirrors pilot VectorLFO manifest shape.
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct VectorEG {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','V','e');
    static constexpr const char*   name        = "VectorEG";
    static constexpr const char*   description = "Dual vector envelope generator.";
    static constexpr BusParam      inputs[]    = {
        {"Freq/Shape CV A", BusKind::cv},
        {"Freq/Shape CV B", BusKind::cv},
        {"Gate A",          BusKind::gate},
        {"Gate B",          BusKind::gate},
    };
    static constexpr BusParam      outputs[]   = {
        {"Env A", BusKind::cv},
        {"Env B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
