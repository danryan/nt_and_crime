#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct AttenuateOffset {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','A','o');
    static constexpr const char*   name        = "AttenuateOffset";
    static constexpr const char*   description = "Attenuvert and offset two CV channels with optional mix";
    static constexpr BusParam      inputs[]    = {{"CV 1", BusKind::cv}, {"CV 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out 1", BusKind::cv}, {"Out 2", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
