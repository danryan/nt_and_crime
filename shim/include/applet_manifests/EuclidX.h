#pragma once
// Vendor deps: (none) -- bjorklund is in shim baseline
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// ManifestNS must be a type (struct) so it can be used as a template parameter
// with per_applet_runtime helpers that access ManifestNS::inputs etc.
namespace per_applet {

struct EuclidX {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','E','x');
    static constexpr const char* name        = "EuclidX";
    static constexpr const char* description = "Dual Euclidean rhythm generator.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Chan1", BusKind::gate},
        {"Chan2", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
