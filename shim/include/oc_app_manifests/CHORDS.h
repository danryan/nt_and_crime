#pragma once
// Vendor app: APP_CHORDS.h (Chords: a four-voice chord sequencer. A scale +
// active-note mask quantizes a root, then a user-defined chord progression voices
// it as a four-note chord on the four DAC channels. Triggers advance the
// progression; CV can map onto root / mask / transpose / octave / quality /
// voicing / inversion / progression-slot / direction / brownian-probability /
// num-chords. The chord slots are edited through the vendor ChordEditor and the
// scale through the vendor ScaleEditor.).
//
// O_C-app manifest for Chords. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/PASSENCORE.h): four CV inputs, four CV outputs,
// four trigger inputs.
//
//   CV out A..D carry the four chord voices (CHORDS_isr -> Update -> the four
//   OC::DAC::set<DAC_CHANNEL_A..D> writes, APP_CHORDS.h:834-837).
//   TR in 1..4 are the advance / reset / playmode triggers; CHORDS::Update reads
//   OC::DigitalInputs::clocked() (all four), then routes per the advance-trigger
//   and playmode settings (TR1/TR2 advance, TR3 for the S+H/CV playmodes).
//   The CV inputs feed the CV-source / CV-mapping settings (root, mask, transpose,
//   octave, quality, voicing, inversion, progression, direction, brownian, num-
//   chords), read through OC::ADC channels.
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct CHORDS {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'C', 'H');
    static constexpr const char* name        = "Chords";
    static constexpr const char* description = "Four-voice chord sequencer: scale-quantized user chord progressions as four-voice pitch (O_C APP_CHORDS port)";

    static constexpr BusParam inputs[] = {
        {"CV 1", BusKind::cv}, {"CV 2", BusKind::cv},
        {"CV 3", BusKind::cv}, {"CV 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Voice A", BusKind::cv}, {"Voice B", BusKind::cv},
        {"Voice C", BusKind::cv}, {"Voice D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Advance", BusKind::gate}, {"Reset", BusKind::gate},
        {"S+H/CV", BusKind::gate}, {"Aux", BusKind::gate},
    };
};
}  // namespace oc_app
