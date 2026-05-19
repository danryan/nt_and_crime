#pragma once

namespace OC {
namespace Strings {
extern const char* const capital_letters[];  // "A","B","C","D",...
// Vendor OC_strings.cpp:67-69. note_names is space-padded ("C ", "C#", ...);
// note_names_unpadded omits trailing spaces. Phase 6 applets (Squanch,
// EnsOscKey, Strum) read note_names_unpadded for tight pitch labels.
extern const char* const note_names[];
extern const char* const note_names_unpadded[];
extern const char* const scale_names[];
extern const char* const scale_names_short[];
}
}
