#pragma once
// Deps: HSRingBufferManager.h (header-only, included by vendor ASR.h);
//       braids quantizer (Quantize/GetScale/SetScale/QuantizerConfigure) --
//       already in shim baseline, no VENDOR_DEPS needed.
// Build token: ASRHemi (avoids collision with plugins/apps/ASR.o).
//
// Input mapping rationale:
//   Hemisphere physical input 0 doubles as both a gate (Clock) and CV (the
//   sampled value). Hemisphere physical input 1 doubles as Freeze gate and
//   Index CV. In NT each bus carries a single signal type, so the manifest
//   exposes four inputs: two gate buses for edge/gate detection (Clock,
//   Freeze) that populate HS::frame.clocked[]/gate_high[], and two CV buses
//   for the sampled signal values that populate HS::frame.inputs[]. This
//   mirrors the DualQuant pattern (4 inputs: 2 gate + 2 CV).
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

struct ASRHemi {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','A','s');
    static constexpr const char* name        = "ASR";
    static constexpr const char* description = "Analog shift register with quantizer.";

    // Gate inputs fill HS::frame.clocked[] and gate_high[]:
    //   input 0 (gate): Clock -- rising edge triggers StartADCLag / buffer advance
    //   input 1 (gate): Freeze -- high gate suppresses WriteValue
    // CV inputs fill HS::frame.inputs[]:
    //   input 2 (cv):   CV -- the sample value written to the ring buffer
    //   input 3 (cv):   Index -- DetentedIn(1) modulates read offset
    static constexpr BusParam inputs[] = {
        {"Clock",  BusKind::gate},
        {"Freeze", BusKind::gate},
        {"CV",     BusKind::cv},
        {"Index",  BusKind::cv},
    };

    static constexpr BusParam outputs[] = {
        {"Out A", BusKind::cv},
        {"Out B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
