#pragma once
// Vendor deps: quant headers (MIDIQuantizer.h via HSMIDI.h)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Calibr8 {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','C','8');
    static constexpr const char*   name        = "Calibr8";
    static constexpr const char*   description = "Pitch calibrator: scale, transpose, and fine-tune two CV channels";
    static constexpr BusParam      inputs[]    = {
        {"Clock", BusKind::gate},
        {"CV 1",  BusKind::cv},
        {"CV 2",  BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Pitch 1", BusKind::cv},
        {"Pitch 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
