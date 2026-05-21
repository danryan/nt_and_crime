#pragma once
// Vendor deps: vec_osc headers (HSVectorOscillator.h, WaveformManager.h).
// All header-only; no .cpp link required. Empty VENDOR_DEPS_VectorMod in Makefile.
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct VectorMod {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','V','m');
    static constexpr const char*   name        = "VectorMod";
    static constexpr const char*   description = "Dual vector oscillator modulator.";
    static constexpr BusParam      inputs[]    = {
        {"Trig 1",   BusKind::gate},
        {"Cycle 1",  BusKind::cv},
        {"Trig 2",   BusKind::gate},
        {"Cycle 2",  BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Ch1 Mod", BusKind::cv},
        {"Ch2 Mod", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
