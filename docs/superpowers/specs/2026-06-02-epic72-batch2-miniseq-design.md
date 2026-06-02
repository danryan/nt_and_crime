# Epic #72 Batch 2: MiniSeq cluster (design)

Vendor pin: `7800d929`. Audit: #71. Epic: #72.

## Scope

Port the three sequencer applets that depend on the vendor `applets/MiniSeq.h`
helper struct: Seq32, SeqPlay7, SwitchSeq. One small shim addition (the
`OC::user_patterns` pattern storage MiniSeq reads) unlocks all three.

## Layer 0 (shipped in this branch, parent-sequential)

`shim/include/OC_patterns.h`: shadow of vendor `OC_patterns.h`. Provides
`OC::Pattern { int16_t notes[16]; }`, the `OC::Patterns` enum
(`PATTERN_USER_COUNT == 8` on this target; the shim does not define
`__IMXRT1062__`), and `extern OC::Pattern user_patterns[8]`. Guard
`OC_PATTERNS_H_` poisons the vendor header. Storage is defined in
`shim/src/globals.cpp` behind `#ifdef NT_HEM_NEED_USER_PATTERNS`, so only the
three batch-2 applet TUs instantiate the 256-byte buffer (no bloat to the other
52 applets, which each privately aggregate globals.cpp).

`MiniSeq.h` itself is vendored unmodified, header-only, `#pragma once`. Each
batch-2 applet includes it (via its vendor applet header). It reinterprets the
16x`int16_t` `notes` buffer as a `uint8_t[32]` step buffer, so the
`int16_t notes[16]` layout is load-bearing; do not change it.

## Canonical recipe

Identical to the batch-1 per-applet recipe (reference: shipped `ClockDivider`
plug-in; manifest + `.cpp` + Catch2 test + Makefile `ALL_APPLET_LIST` and
`VENDOR_DEPS_<A> :=`). The only delta: each batch-2 applet `.cpp` MUST
`#define NT_HEM_NEED_USER_PATTERNS 1` before its first include so the
`globals.cpp` aggregation instantiates `OC::user_patterns`. All three are
`HemisphereApplet` subclasses, 2 inputs / 2 outputs, non-trivial
`OnDataRequest` (pack helper required).

## Per-applet worklist

- Seq32 (`applets/Seq32.h`, suggested GUID `HmS3`): single `MiniSeq seq`, 32-step
  note sequencer with a write/edit cursor and a quantizer edit mode (quantizer
  already shipped). In Clock/Reset (gate), out CV/gate per vendor SetHelp.
- SeqPlay7 (`applets/SeqPlay7.h`, suggested GUID `HmP7`): `MiniSeqPlayer` array
  (7 players), pattern playback. In Clock/Reset (gate), out CV/gate.
- SwitchSeq (`applets/SwitchSeq.h`, suggested GUID `HmSs`): four embedded
  `MiniSeq miniseq[4]`, mode plus CV selects the active sequence. In Clock/CV
  (gate/cv), out CV/gate.

Each implementer reads the vendor header fully and derives manifest names from
`SetHelp`, the pack layout from `OnDataRequest`, and the customUI routing from
`OnEncoderMove`/`OnButtonPress`. Verify the suggested GUID is unique
(`grep -rhoE "NT_MULTICHAR\('H','m'" shim/include/applet_manifests/`).

## Fidelity limit (documented, acceptable)

The 32-step sequence (32 steps x 6 bits = 192 bits) does NOT fit the 64-bit
hemi serialise blob, and `OC::user_patterns` is per-TU storage with no preset
persistence path. So a programmed sequence does NOT survive a preset save/reload
on hardware. This matches the vendor reality (on Phazerville the pattern lives
in a shared EEPROM page the HEMISPHERE app serialises separately, not the applet
blob) and the documented CHORDS `user_chords` precedent. The applet's
`OnDataRequest`/`OnDataReceive` still round-trips whatever scalar config it
packs (length, pattern index, mode); the test covers that, not the full step
buffer.

## 10x clocked-multiplier coverage

All three advance step state inside `if (Clock(ch))`, so SHAPE 2 (round-trip
plus state injection, drop bus-level fire-count assertions) is the default;
state-inject via `OC::user_patterns` or the applet's setters and assert the
observable output, do not count fires.

## Spec footer

### Recipe spot-check

The recipe is the shipped batch-1 recipe plus one `#define`. 67 applets now ship
through it. Layer 0 is the only new shim surface and is proven non-breaking
(an existing applet test stays green with `OC_patterns.h` added to globals.cpp).

### Per-entry verification (3 of 3 traced against vendor `7800d929`)

- Seq32 (`applets/Seq32.h:23`): `class Seq32 : public HemisphereApplet`,
  `#include "MiniSeq.h"` line 21, `MiniSeq seq` line 54, `OnDataReceive` line 251
  (pack helper required). CONSISTENT.
- SeqPlay7 (`applets/SeqPlay7.h:23`): `class SeqPlay7 : public HemisphereApplet`,
  `#include "MiniSeq.h"` line 21, `struct MiniSeqPlayer { MiniSeq seq; }` line 38,
  `OnDataReceive` line 194. CONSISTENT.
- SwitchSeq (`applets/SwitchSeq.h:45`): `class SwitchSeq : public HemisphereApplet`,
  `#include "MiniSeq.h"` line 33, `MiniSeq miniseq[4]` line 151, `OnDataReceive`
  line 125. CONSISTENT.

### Shim prereq verification

`OC::user_patterns` + `OC::Patterns::PATTERN_USER_COUNT` were the only missing
deps (#71). Both are provided by the Layer-0 `OC_patterns.h` shim shadow plus the
guarded `globals.cpp` storage. `CONSTRAIN` and `random()` are already shim hem
macros. No `VENDOR_DEPS` link needed (MiniSeq.h is header-only).
