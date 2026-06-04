#pragma once
// Vendor deps: (none) -- hMIDIOut writes HS::frame.MIDIState Send*; no extra cpp.
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

// hMIDIOut: CV -> MIDI. The vendor applet reads Gate(0) plus In(0) (pitch) and
// In(1) (a selectable second function: velocity / CC / aftertouch / pitch bend)
// and emits MIDI through HS::frame.MIDIState.Send* (Layer 0b -> NT_sendMidi*).
// It writes no CV output of its own.
//
// Input layout matches the runtime's by-kind indexing: the first gate input
// feeds vendor Gate(0); the first/second CV inputs feed vendor In(0)/In(1).
//   input 0 = Gate   (gate)  -> Gate(0)
//   input 1 = Pitch  (cv)    -> In(0)
//   input 2 = Func   (cv)    -> In(1)
// One inert CV output is declared because the per-applet runtime requires a
// non-empty outputs[]; the vendor applet never calls Out(), so it stays at 0.
struct hMIDIOut {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','M','o');
    static constexpr const char* name        = "MIDI Out";
    static constexpr const char* description = "CV to MIDI: notes, velocity, CC, bend, aftertouch.";

    static constexpr BusParam inputs[] = {
        {"Gate",  BusKind::gate},
        {"Pitch", BusKind::cv},
        {"Func",  BusKind::cv},
    };

    static constexpr BusParam outputs[] = {
        {"Unused", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
