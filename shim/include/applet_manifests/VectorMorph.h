#pragma once
// Vendor deps: vec_osc headers (HSVectorOscillator + WaveformManager), header-only,
// in shim baseline. No .cpp link required.
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct VectorMorph {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','V','m');
    static constexpr const char*   name        = "VectorMorph";
    static constexpr const char*   description = "Dual vector-oscillator phase morpher.";
    static constexpr BusParam      inputs[]    = {
        {"Phase 1 CV", BusKind::cv},
        {"Phase 2 CV", BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Out A", BusKind::cv},
        {"Out B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
