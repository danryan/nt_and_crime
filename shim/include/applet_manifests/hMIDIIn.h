#pragma once
// Vendor deps: (none) -- hMIDIIn reads HS::frame.MIDIState, no extra vendor cpp.
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet {

// hMIDIIn: MIDI -> CV. MIDI arrives through the NT firmware midiMessage /
// midiRealtime callbacks (Layer 0c), NOT through bus inputs, so this applet has
// no functional CV/gate inputs of its own. The per-applet runtime is built
// around the universal 2-in / 2-out Hemisphere shape, so two gate inputs are
// declared as inert placeholders: the vendor hMIDIIn Controller() reads only
// HS::frame.MIDIState (mapping[].output and clock_run) and never calls Gate()
// or In(), so these inputs are populated by the runtime but ignored by the
// applet. The two outputs carry the two mapped MIDI functions (e.g. Note CV on
// A, clock-run gate on B).
struct hMIDIIn {
    static constexpr uint32_t    guid        = NT_MULTICHAR('H','m','M','i');
    static constexpr const char* name        = "MIDI In";
    static constexpr const char* description = "MIDI to CV: notes, velocity, CC, clock.";

    static constexpr BusParam inputs[] = {
        {"Unused 1", BusKind::gate},
        {"Unused 2", BusKind::gate},
    };

    static constexpr BusParam outputs[] = {
        {"Out A", BusKind::cv},
        {"Out B", BusKind::cv},
    };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};

}  // namespace per_applet
