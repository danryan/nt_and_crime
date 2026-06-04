#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// ManifestNS must be a type (struct) so it can be used as a template parameter
// with per_applet_runtime helpers that access ManifestNS::inputs etc.
namespace per_applet {

struct SequenceX {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','S','x');
    static constexpr const char* name        = "Seq8";
    static constexpr const char* description = "8-step CV sequencer with mutable steps.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"CV",     BusKind::cv},
        {"Step 1", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
