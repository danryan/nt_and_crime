#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// ManifestNS must be a type (struct) so it can be used as a template parameter
// with per_applet_runtime helpers that access ManifestNS::inputs etc.
namespace per_applet {

struct TrigSeq16 {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','T','6');
    static constexpr const char* name        = "Trig16";
    static constexpr const char* description = "16-step trigger sequencer.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Trg",     BusKind::gate},
        {"Inverse", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
