#pragma once
// Vendor deps: SegmentDisplay.h (header-only; SegmentDisplay::digit out-of-class
// definition is already provided by shim/src/globals.cpp).
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct Binary {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','B','n');
    static constexpr const char*   name        = "Binary Counter";
    static constexpr const char*   description = "4-bit binary counter: gates + CVs to binary sum and bit count.";
    // Input ordering: gates first (positions 0,1 -> frame.gate_high[0,1]),
    // then CVs (positions 2,3 -> frame.inputs[2,3]).
    // Binary.cpp step_impl copies inputs[2,3] to inputs[0,1] after populate,
    // so Binary's In(0)/In(1) reads work correctly.
    static constexpr BusParam      inputs[]    = {
        {"Gate A", BusKind::gate},
        {"Gate B", BusKind::gate},
        {"CV A",   BusKind::cv  },
        {"CV B",   BusKind::cv  },
    };
    static constexpr BusParam      outputs[]   = {
        {"Binary", BusKind::cv},
        {"Count",  BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
