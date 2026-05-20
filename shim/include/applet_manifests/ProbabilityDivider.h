#pragma once
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// Vendor deps: (none)
// HSProbLoopLinker is included transitively through ProbabilityDivider.h but
// is header-only and vendor-located; no shim re-export is needed.
//
// Singleton-private-to-.o gotcha:
//   HSProbLoopLinker is a singleton. In the per-applet plug-in shape, that
//   singleton is PRIVATE to this .o. ProbLoopLinker::instance is a static
//   pointer with definition in HSProbLoopLinker.h; the definition compiles
//   into this translation unit only. If ProbabilityMelody is ever ported as
//   a separate .o, the two singletons will be INDEPENDENT. They will not
//   share linker state (loop seed, loopStep, isLooping, registered[]).
//   This is acceptable for the pilot release because ProbabilityMelody is
//   not in scope here, but the gotcha must be documented for the mass-port
//   release so that any ProbabilityMelody implementer knows to either
//   (a) co-link both applets into one .o, or (b) replace the singleton
//   with a firmware-owned shared-data mechanism.

namespace per_applet {

struct ProbabilityDivider {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','P','d');
    static constexpr const char* name        = "ProbabilityDivider";
    static constexpr const char* description = "Probabilistic clock divider with seeded loop.";
    static constexpr BusParam    inputs[]    = {
        { "Clock", BusKind::gate },
        { "Reset", BusKind::gate },
    };
    static constexpr BusParam    outputs[]   = {
        { "Out A", BusKind::gate },
        { "Out B", BusKind::gate },
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
