#pragma once
// Vendor deps: MiniSeq.h (header-only, included by Seq32.h).
// The consuming .cpp MUST define NT_HEM_NEED_USER_PATTERNS 1 before any
// #include so that globals.cpp instantiates OC::user_patterns[8].
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct Seq32 {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','S','3');
    static constexpr const char* name        = "Seq32";
    static constexpr const char* description = "32-step note sequencer with write/edit cursor.";

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
