#pragma once
// Vendor deps: quant headers (HS::Quantize() on shim base class -- no new shim surface)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct OffsetQuant {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','O','q');
    static constexpr const char*   name        = "Offset Quant";
    static constexpr const char*   description = "Dual offset-and-quantize: maps CV to a pitch range then quantizes to scale";

    // Two clock gates (one per channel) then two CV inputs (one per channel).
    // Controller() uses Clock(ch) and In(ch) with ch in {0,1}; the runtime
    // maps gate_ch=0..1 to clocked[0..1] and cv_ch=0..1 to inputs[0..1].
    static constexpr BusParam inputs[] = {
        {"Clock 1", BusKind::gate},
        {"Clock 2", BusKind::gate},
        {"CV 1",    BusKind::cv},
        {"CV 2",    BusKind::cv},
    };

    static constexpr BusParam outputs[] = {
        {"Out 1", BusKind::cv},
        {"Out 2", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
