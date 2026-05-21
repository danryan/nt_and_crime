#pragma once
// Vendor deps: (none)
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {
struct TLNeuron {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','T','N');
    static constexpr const char*   name        = "TL Neuron";
    static constexpr const char*   description = "Threshold-logic neuron: fires axon when weighted dendrite sum exceeds threshold";

    // Input 0: Dendrite 1 (gate). Input 1: Dendrite 2 (gate). Input 2: Dendrite 3 (cv).
    static constexpr BusParam inputs[] = {
        {"Dendrite 1", BusKind::gate},
        {"Dendrite 2", BusKind::gate},
        {"Dendrite 3", BusKind::cv},
    };

    // Output 0: Axon gate. Output 1: Axon gate (copy).
    static constexpr BusParam outputs[] = {
        {"Axon Out 1", BusKind::gate},
        {"Axon Out 2", BusKind::gate},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}  // namespace per_applet
