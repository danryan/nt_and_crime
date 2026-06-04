#pragma once
// Vendor deps: peaks_bytebeat.cpp linked; util/util_history.h is header-only.
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct BitBeat {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','B','b');
    static constexpr const char* name        = "BitBeat";
    static constexpr const char* description = "Dual bytebeat rhythm/CV generator.";

    static constexpr BusParam inputs[] = {
        {"Reset1", BusKind::gate},
        {"Reset2", BusKind::gate},
    };

    // ByteBeat output is full-scale bipolar CV (sample - 32768 scaled to
    // HEMISPHERE_3V_CV), functionally analogous to audio/modulation CV.
    static constexpr BusParam outputs[] = {
        {"Beat1", BusKind::cv},
        {"Beat2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
