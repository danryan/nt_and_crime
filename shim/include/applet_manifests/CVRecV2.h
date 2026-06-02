#pragma once
// Vendor deps: SegmentDisplay (header-only against shim; out-of-class
// SegmentDisplay::digit definition lives in shim/src/globals.cpp).
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct CVRecV2 {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','C','v');
    static constexpr const char* name        = "CVRec";
    static constexpr const char* description = "Dual CV recorder/player.";

    static constexpr BusParam inputs[] = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Play 1", BusKind::cv},
        {"Play 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
