#pragma once
// Shim shadow of vendor O_C-Phazerville/software/src/OC_patterns.h. Mirrors the
// vendor OC::Pattern storage and the Patterns enum that the MiniSeq vendor
// helper (applets/MiniSeq.h) and the sequencer applets Seq32 / SeqPlay7 /
// SwitchSeq depend on. Defining the vendor include guard OC_PATTERNS_H_ poisons
// the vendor header so that if it is ever quote-included from inside the vendor
// tree it self-suppresses and this shim wins (the include-guard-poison
// discipline; see CLAUDE.md "Shadowing a vendor header quote-included from
// inside another vendor header").
//
// PATTERN_USER_COUNT resolves to 8 on this target: the shim does not define
// __IMXRT1062__, so the vendor enum's auto-increment branch applies (8 user
// pattern slots). MiniSeq's SEQUENCE_COUNT is this value, and SwitchSeq (4
// slots) and SeqPlay7 (7 slots) index within it.
//
// Pattern::notes is int16_t[16] = 32 bytes, which MiniSeq reinterprets as a
// uint8_t[32] step buffer (32 steps, one byte each). The byte layout must match
// the vendor exactly for that reinterpret to be correct, so keep notes as
// int16_t[16].
#ifndef OC_PATTERNS_H_
#define OC_PATTERNS_H_

#include <cstdint>

namespace OC {

struct Pattern {
    int16_t notes[16];
};

static constexpr int kMaxPatternLength = 16;
static constexpr int kMinPatternLength = 2;

namespace Patterns {
enum {
    PATTERN_USER_0_1,
    PATTERN_USER_1_1,
    PATTERN_USER_2_1,
    PATTERN_USER_3_1,
    PATTERN_USER_0_2,
    PATTERN_USER_1_2,
    PATTERN_USER_2_2,
    PATTERN_USER_3_2,
    PATTERN_USER_COUNT,
};

static const int PATTERN_NONE = -1;
static const int NUM_PATTERNS_PER_CHAN = 4;
static const int kMin = kMinPatternLength;
static const int kMax = kMaxPatternLength;
}  // namespace Patterns

// Storage is defined in shim/src/globals.cpp ONLY when the consuming applet TU
// defines NT_HEM_NEED_USER_PATTERNS, so the 256-byte buffer does not bloat the
// other per-applet plug-ins.
extern Pattern user_patterns[Patterns::PATTERN_USER_COUNT];

}  // namespace OC

#endif  // OC_PATTERNS_H_
