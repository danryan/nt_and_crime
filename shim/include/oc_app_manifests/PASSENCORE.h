#pragma once
// Vendor app: APP_PASSENCORE.h (Passencore: a voice-leading chord sequencer
// driven by trigger inputs; emits a four-voice chord as 1V/oct pitch).
//
// O_C-app manifest for Passencore. Mirrors the per-app manifest shape
// (shim/include/oc_app_manifests/Harrington1200.h): it declares the fixed
// 12-row I/O routing block the per-app runtime emits (oc_runtime::emit_io_params)
// as four CV inputs, four CV outputs, and four trigger inputs. The names
// document the vendor app's wiring:
//
//   CV out A..D carry the four chord voices (play_chord -> OC::DAC::set_pitch on
//   DAC_CHANNEL_A..D, APP_PASSENCORE.h:333).
//   TR in 1..4 are the sample / target / passing / reset triggers; which logical
//   trigger maps to which physical input is itself a setting (SAMPLE_TRIGGER,
//   TARGET_TRIGGER, PASSING_TRIGGER, RESET_TRIGGER), read through
//   DIGITAL_INPUT_MASK in PASSENCORE::ISR (APP_PASSENCORE.h:827-830). The names
//   here list the default assignment.
//   The CV inputs feed the CV3/CV4 roles (root / bass / include / color), which
//   read OC::ADC channels (APP_PASSENCORE.h:584-663).
//
// The guid uses the "OC" prefix so it never collides with the Hemisphere "Hm"
// space or the composer host guids (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct PASSENCORE {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'P', 'S');
    static constexpr const char* name        = "Passencore";
    static constexpr const char* description = "Voice-leading chord sequencer: target/passing chords as four-voice 1V/oct pitch (O_C APP_PASSENCORE port)";

    static constexpr BusParam inputs[] = {
        {"CV 1", BusKind::cv}, {"CV 2", BusKind::cv},
        {"CV 3", BusKind::cv}, {"CV 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Voice A", BusKind::cv}, {"Voice B", BusKind::cv},
        {"Voice C", BusKind::cv}, {"Voice D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Sample", BusKind::gate}, {"Target", BusKind::gate},
        {"Passing", BusKind::gate}, {"Reset", BusKind::gate},
    };
};
}  // namespace oc_app
