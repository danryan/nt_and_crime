#pragma once
// Vendor deps: (none) -- braids scales/quantizer are in shim baseline
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// ManifestNS must be a type (struct) so it can be used as a template parameter
// with per_applet_runtime helpers that access ManifestNS::inputs etc.
namespace per_applet {

struct TB3PO {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','T','3');
    static constexpr const char* name        = "TB-3PO";
    static constexpr const char* description = "TB-303 style acid pattern generator.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Regen", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Pitch", BusKind::cv},
        {"Gate",  BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
