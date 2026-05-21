#pragma once
// Vendor deps: quant headers (HS::Quantize, QuantizerConfigure, OC::Scales,
//              braids::Scale, OC::Strings::note_names_unpadded,
//              scale_names_short, rotl32) — all in shim baseline.

#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Chordinator {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','h');
    static constexpr const char*   name        = "Chordinator";
    static constexpr const char*   description = "Chord quantizer: root + harmony voice from scale mask";
    static constexpr BusParam      inputs[]    = {
        {"Clk 1",   BusKind::gate},
        {"Clk 2",   BusKind::gate},
        {"Root CV", BusKind::cv},
        {"Harm CV", BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Root", BusKind::cv},
        {"Harm", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
