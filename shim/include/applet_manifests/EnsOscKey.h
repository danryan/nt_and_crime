#pragma once
// Vendor deps: quant headers (HS::Quantize via shim baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct EnsOscKey {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','E','k');
    static constexpr const char*   name        = "EnsOscKey";
    static constexpr const char*   description = "Ensemble Oscillator Key: quantizes pitch and outputs chord quality CV";
    static constexpr BusParam      inputs[]    = {{"Pitch", BusKind::cv}, {"Octave", BusKind::cv}, {"Clock", BusKind::gate}};
    static constexpr BusParam      outputs[]   = {{"Note", BusKind::cv}, {"Scale", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
