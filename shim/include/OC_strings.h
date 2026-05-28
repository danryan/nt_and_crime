#pragma once

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
}

// Harrington 1200 trigger delay ticks lookup. Vendor OC_strings.h:70.
extern const uint8_t trigger_delay_ticks[];

}
