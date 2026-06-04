#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct TrigSeq {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','T','s');
    static constexpr const char* name        = "Trig8x2";
    static constexpr const char* description = "Dual 8-step trigger sequencer.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Trg Ch1", BusKind::gate},
        {"Trg Ch2", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
