#pragma once
// Define the vendor include guard so a quote-include of "OC_strings.h" from
// inside a vendor app header (APP_H1200.h:29, which resolves to its vendor
// sibling first, not through -Ishim/include) becomes a no-op once this shim
// shadow has been included ahead of the vendor app header. Without this the
// vendor OC_strings.h would redefine kNumDelayTimes and the global note_name().
// Same poison technique as OC_digital_inputs.h.
#ifndef OC_STRINGS_H_
#define OC_STRINGS_H_
#endif

#include <cstdint>

namespace OC {

// Harrington 1200 trigger delay lookup. Vendor OC_strings.h:10.
static const int kNumDelayTimes = 8;

namespace Strings {
extern const char* const capital_letters[];  // "A","B","C","D",...
// Vendor OC_strings.cpp:67-69. note_names is space-padded ("C ", "C#", ...);
// note_names_unpadded omits trailing spaces. Quantizer-using applets
// (Squanch, EnsOscKey, Strum) read note_names_unpadded for tight pitch labels.
extern const char* const note_names[];
extern const char* const note_names_unpadded[];
extern const char* const scale_names[];
extern const char* const scale_names_short[];
// Harrington 1200 app dependencies. Vendor OC_strings.h:38,47,70.
extern const char* const cv_input_names_none[];
extern const char* const trigger_delay_times[kNumDelayTimes];
// BBGEN (APP_BBGEN) settings labels. Vendor OC_strings.h:35,39.
// trigger_input_names labels the per-ball Trigger-input enum (TR1..TR4); no_yes
// labels the Hard-reset bool.
extern const char* const trigger_input_names[];
extern const char* const no_yes[];
// BYTEBEATGEN (APP_BYTEBEATGEN) Equation-setting value labels. Vendor
// OC_strings.cpp:127. Sixteen bytebeat-equation names ("hope" .. "Orac").
extern const char* const bytebeat_equation_names[];
// POLYLFO (APP_POLYLFO) Tap-tempo bool label. Vendor OC_strings.cpp:117.
extern const char* const off_on[];
// ENVGEN (APP_ENVGEN) envelope-setting value labels. Vendor OC_strings.cpp:131,156,160.
extern const char* const envelope_shapes[];
extern const char* const reset_behaviours[];
extern const char* const falling_gate_behaviours[];
// Trigger-or-CV-or-internal source labels with a leading "none". Vendor
// OC_strings.cpp:75 (ENVGEN Trigger-input + Eucl-reset settings).
extern const char* const trigger_input_names_none[];
// PASSENCORE (APP_PASSENCORE) scale-editor / chord-editor labels. Vendor
// OC_strings.h:26-48. Referenced at template-definition scope by the vendor
// OC_scale_edit.h and OC_chords_edit.h that APP_PASSENCORE.h pulls in (even with
// the chord editor commented out, the template bodies are parsed).
extern const char* const scale_degrees_maj[];
extern const char* const scale_degrees_min[];
extern const char* const accidentals[];
extern const char* const scale_id[];
extern const char* const channel_id[];
extern const char* const scaling_string[];
extern const char* const chord_property_names[];
// DQ (APP_DQ / Meta-Q) setting value labels. Vendor OC_strings.h:23,37,50.
// cv_input_names is the no-leading-none CV source list; channel_trigger_sources
// labels the per-channel trigger setting; TM_aux_cv_destinations labels the
// Turing-machine aux-CV destination.
extern const char* const cv_input_names[];
extern const char* const channel_trigger_sources[];
extern const char* const TM_aux_cv_destinations[];
}

// Harrington 1200 trigger delay ticks lookup. Vendor OC_strings.h:70.
extern const uint8_t trigger_delay_ticks[];

}

// Free function at global scope (vendor OC_strings.h:74). Harrington 1200's
// menu and screensaver call note_name(int) unqualified to label the rendered
// triad notes. Mirrors the vendor body byte-for-byte: index the space-padded
// note_names table by the pitch class, biased +120 so negative notes wrap.
inline const char *note_name(int note) {
  return OC::Strings::note_names[(note + 120) % 12];
}
