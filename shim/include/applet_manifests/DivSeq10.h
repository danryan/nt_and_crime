#pragma once
// Vendor deps: (none) -- util/clkdivmult.h is in shim baseline
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// ManifestNS must be a type (struct) so it can be used as a template parameter
// with per_applet_runtime helpers that access ManifestNS::inputs etc.
namespace per_applet {

struct DivSeq10 {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','D','t');
    static constexpr const char* name        = "DivSeq10";
    static constexpr const char* description = "Division sequencer with 10 steps.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Trig 1", BusKind::gate},
        {"Trig 2", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
