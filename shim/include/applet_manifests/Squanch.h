#pragma once
// Vendor deps: quant headers (HS::Quantize() on shim base class -- no new shim surface)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Squanch {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','S','q');
    static constexpr const char*   name        = "Squanch";
    static constexpr const char*   description = "Dual quantizer with per-channel shift, note wrap, shared scale/root";
    static constexpr BusParam      inputs[]    = {{"Clock",  BusKind::gate},
                                                  {"+Oct",   BusKind::gate},
                                                  {"Signal", BusKind::cv},
                                                  {"Trn",    BusKind::cv}};
    static constexpr BusParam      outputs[]   = {{"Out 1",  BusKind::cv},
                                                  {"Out 2",  BusKind::cv}};

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
