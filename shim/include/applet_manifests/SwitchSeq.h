#pragma once
// Vendor deps: MiniSeq.h (header-only), OC_patterns.h (via NT_HEM_NEED_USER_PATTERNS)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct SwitchSeq {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','S','s');
    static constexpr const char* name        = "SwitchSeq";
    static constexpr const char* description = "4-sequence switcher with mode and CV selection.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"CV Ch 2", BusKind::cv},
    };

    static constexpr BusParam outputs[] = {
        {"Ch 1", BusKind::cv},
        {"Ch 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
