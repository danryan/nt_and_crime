#pragma once
// Vendor deps: MiniSeq.h (header-only, via SeqPlay7.h).
// Consuming .cpp MUST #define NT_HEM_NEED_USER_PATTERNS 1 before any #include
// so that globals.cpp instantiates OC::user_patterns[8].

#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct SeqPlay7 {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','P','7');
    static constexpr const char* name        = "SeqPlay7";
    static constexpr const char* description = "7-player pattern sequencer with repeats.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Pitch", BusKind::cv},
        {"Gate",  BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
