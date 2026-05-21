#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct EnvFollow {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','E','f');
    static constexpr const char*   name        = "EnvFollow";
    static constexpr const char*   description = "Envelope follower: tracks signal amplitude with optional duck mode";
    static constexpr BusParam      inputs[]    = {{"CV 1", BusKind::cv}, {"CV 2", BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out 1", BusKind::cv}, {"Out 2", BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
