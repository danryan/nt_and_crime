#pragma once
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

// Vendor deps: (none)
// HSProbLoopLinker is included transitively through ProbabilityMelody.h but
// is header-only and vendor-located; no shim re-export is needed.
//
// Singleton-private-to-.o note:
//   HSProbLoopLinker is a singleton. In the per-applet plug-in shape each .o
//   has its own independent copy of ProbLoopLinker::instance. ProbabilityMelody
//   and ProbabilityDivider therefore have INDEPENDENT singletons (separate seed,
//   loopStep, isLooping state). They will not communicate across .o boundaries
//   in the NT runtime.
//
// CV input labels are mode-dependent (probmelod::cv_modes[cv_mode]); stable
// static names "CV 1"/"CV 2" are used here so the bus table does not change
// at runtime.

namespace per_applet {

struct ProbabilityMelody {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','P','M');
    static constexpr const char* name        = "ProbabilityMelody";
    static constexpr const char* description = "Probabilistic melodic sequencer with weighted notes.";

    static constexpr BusParam inputs[] = {
        { "Clock 1", BusKind::gate },
        { "Clock 2", BusKind::gate },
    };

    static constexpr BusParam outputs[] = {
        { "Pitch 1", BusKind::cv },
        { "Pitch 2", BusKind::cv },
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
