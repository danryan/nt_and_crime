#pragma once
// Vendor deps: (none) — HSRelabiManager.h is header-only and vendor-located;
// vector_osc is in shim baseline.

#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct Relabi_manifest {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','R','l');
    static constexpr const char* name        = "Relabi";
    static constexpr const char* description = "Relabi cross-modulating LFO trio";
    static constexpr BusParam    inputs[]    = {
        {"Clock", BusKind::gate},
        {"Reset", BusKind::gate},
    };
    static constexpr BusParam    outputs[]   = {
        {"Out A", BusKind::cv},
        {"Out B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
