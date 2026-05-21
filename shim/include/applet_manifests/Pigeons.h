#pragma once
// Vendor deps: HS::QuantizerLookup() (shim baseline)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Pigeons {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','P','g');
    static constexpr const char*   name        = "Pigeons";
    static constexpr const char*   description = "Pigeons: dual Fibonacci-modulo pitch sequence with quantization";
    static constexpr BusParam      inputs[]    = {{"Clock 1", BusKind::gate}, {"Clock 2", BusKind::gate},
                                                  {"Modulo 1", BusKind::cv},   {"Modulo 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Pitch 1", BusKind::cv}, {"Pitch 2", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
