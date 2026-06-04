#pragma once
// Vendor deps: braids quantizer (shim baseline, no VENDOR_DEPS needed)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// ManifestNS must be a type (struct) so it can be used as a template parameter
// with per_applet_runtime helpers that access ManifestNS::inputs etc.
namespace per_applet {

struct ShiftReg {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','S','R');
    static constexpr const char* name        = "ShiftReg";
    static constexpr const char* description = "Shift-register Turing machine with quantized output.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"p Gate", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"5bits Q", BusKind::cv},
        {"8bits V", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
