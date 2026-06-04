#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// ManifestNS must be a type (struct) so it can be used as a template parameter
// with per_applet_runtime helpers that access ManifestNS::inputs etc.
namespace per_applet {

struct Palimpsest {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','P','m');
    static constexpr const char* name        = "Palimpsest";
    static constexpr const char* description = "Accent memory sequencer with compose/decompose.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Brush", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Output", BusKind::cv},
        {"Trigger", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
