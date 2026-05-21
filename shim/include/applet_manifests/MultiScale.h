#pragma once
// Vendor deps: quant headers (OC_scales.h, braids_quantizer.h via HSUtils.h)
// Uses OC::Scales::SCALE_SEMI and braids::Quantizer; both are in shim baseline.
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct MultiScale {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','M','s');
    static constexpr const char*   name        = "MultiScale";
    static constexpr const char*   description = "Quantizer with 4 user-definable scales, selectable via CV";
    static constexpr BusParam      inputs[]    = {
        {"Clock",   BusKind::gate},
        {"UnClock", BusKind::gate},
        {"CV",      BusKind::cv},
        {"Scale",   BusKind::cv},
    };
    static constexpr BusParam      outputs[]   = {
        {"Pitch",   BusKind::cv},
        {"Trigger", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
