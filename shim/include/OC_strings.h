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
