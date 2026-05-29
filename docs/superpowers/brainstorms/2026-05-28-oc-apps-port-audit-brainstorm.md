# O_C apps port audit and batch selection

Status: audit complete, batch not yet selected.
Vendor: `vendor/O_C-Phazerville` pinned at `7800d929`.
Foundation: merged in PR #31 (issue #29). Shipped reference ports: Low-rents
(`APP_LORENZ.h`), Harrington 1200 (`APP_H1200.h`).

## Purpose

Reconcile the full `APP_*.h` catalog to the candidates that can be ported on the
O_C apps foundation, categorize each by porting cost, and propose a bounded pilot
batch. This is the audit phase that precedes a port-batch brainstorm/spec/plan.

## Candidate reconciliation

The vendor tree has 28 `APP_*.h`. Excluded:

- Already shipped: `APP_LORENZ` (Low-rents), `APP_H1200` (Harrington 1200).
- Hemisphere hosts, handled separately in this project: `APP_HEMISPHERE`,
  `APP_QUADRANTS`.
- System and utility, not user-facing ports: `APP_Backup`, `APP_SETTINGS`,
  `APP_REFS`, `APP_CALIBR8OR`.

That leaves 20 nominal candidates. Load-bearing audit finding: three of those 20
are NOT `OC::App` full-screen apps. They inherit `HSApplication` (the Hemisphere
application framework) and depend on Teensy `usbMIDI`, `EEPROM`, and a hard
`#include "src/drivers/display.h"`. They are out of scope for this foundation and
belong to a separate HSApplication track:

- `APP_ENIGMA` (Turing machine; HSApplication, sysex, EEPROM-on-resume).
- `APP_MIDI` (Captain MIDI; HSApplication, Teensy display.h hard-include, usbMIDI,
  EEPROM).
- `APP_NeuralNetwork` (HSApplication, sysex send/receive, EEPROM, 216 settings).

True `OC::App` candidate set: 17.

## Categorization (17 OC::App candidates)

EASY means it fits the shipped recipe with no new shared shim subsystem. MEDIUM
means it needs a bounded, reusable shim addition. HARD means a large new
subsystem or a custom full-screen editor beyond the settings-menu model.

| App | Verdict | Output type | Vendor .cpp deps | Key foundation gap |
| --- | --- | --- | --- | --- |
| PONGGAME | EASY | modulation + gates | none | none (0 settings; tests the no-settings path) |
| FPART | EASY | pitch sequencer | none | none (all headers shadowed) |
| BBGEN | EASY | full-scale modulation | none | none (peaks_bouncing_balls is header-only) |
| BYTEBEATGEN | EASY | full-scale modulation | peaks_bytebeat | none (conditional-menu pattern already supported) |
| SCALEEDITOR | EASY | pitch | braids_quantizer, OC_scales | SegmentDisplay helper (thin) |
| THEDARKESTTIMELINE | EASY | pitch + gates | braids_quantizer, OC_patterns | none (MIDI out is native on NT) |
| POLYLFO | MEDIUM | full-scale modulation | frames_poly_lfo | VBiasManager stub (VOR is conditional) |
| ENVGEN | MEDIUM | modulation envelope | peaks_multistage_envelope, bjorklund | OC_euclidean_mask_draw |
| DQ | MEDIUM | pitch quantizer | braids_quantizer | OC_visualfx, OC_scale_edit |
| ASR | MEDIUM | pitch quantizer | braids_quantizer, peaks_bytebeat | OC_visualfx, OC_scale_edit, OC_strings tables |
| AUTOMATONNETZ | MEDIUM | pitch + triggers | none | util_sync critical-section stub, tonnetz headers, mode_names |
| PASSENCORE | MEDIUM | pitch + gates | none (header tables) | OC_chords, OC_input_map, quantizer tables |
| SCENES | MEDIUM | full-scale modulation | OC_input_map | FreqMeasure is Teensy-only (loses tempo-from-audio), preset I/O |
| WAVEFORMEDITOR | MEDIUM | synthesis | none | HSVectorOscillator, pixel-grid canvas editor |
| CHORDS | HARD | pitch quantizer (4ch) | braids_quantizer, OC_chords | OC_chords_edit, OC_input_maps, 2-page menu |
| QQ (Quantermain) | HARD | pitch quantizer (4ch) | braids_quantizer | OC_scale_edit template, 4 x 53 settings, MI generators |
| SEQ | HARD | pitch + gate (4+4) | peaks_multistage_envelope, OC_patterns | OC_sequence_edit full-screen editor, 59 x 2 settings |

## Reusable shim additions implied by the MEDIUM tier

Several MEDIUM apps share the same missing pieces. Building these once unlocks a
cluster, so they are spec candidates for a shared preparatory layer rather than
per-app work:

- `OC_scale_edit.h` scale-mask editor widget: needed by DQ, ASR, and the HARD
  quantizers (QQ, CHORDS, SEQ). Highest-leverage single addition.
- `OC_visualfx.h` history/scope helpers: DQ, ASR, QQ, SEQ.
- braids quantizer plus scale tables as a linked vendor unit: every quantizer app.
- peaks libraries (`peaks_bytebeat`, `peaks_multistage_envelope`): BYTEBEATGEN,
  ASR, ENVGEN, SEQ.
- `OC_strings.h` extended enum tables (cv input names, trigger delays, sequence and
  bytebeat equation names): ASR, and others.

## Recommended pilot batch

Six EASY `OC::App` apps, none of which need a new shared subsystem. They validate
the recipe at scale and exercise three distinct shapes the two shipped apps did
not:

- PONGGAME and SCALEEDITOR: the zero-settings path (no `SettingsBase` parameter
  table; `numParameters == kIoParamCount`).
- BBGEN and BYTEBEATGEN: full-scale modulation outputs (confirm the 16-bit DAC
  scaling from PR #31 on more apps).
- FPART and THEDARKESTTIMELINE: pitch and pitch-plus-gate sequencing with native
  MIDI.

Defer the MEDIUM tier to a follow-on phase whose Layer 0 builds `OC_scale_edit`
and `OC_visualfx` first (they gate the quantizer cluster). Defer the HARD tier
until the editor subsystems exist. Track the HSApplication trio (ENIGMA, MIDI,
NeuralNetwork) under a separate HSApplication-apps initiative.

## Spec follow-ups carried from #29 (separate small PR, before the pilot batch)

- Relocate the three OC string tables out of `shim/src/globals.cpp` into a proper
  strings unit.
- ADC scale-unification hardware check.
- Inert manifest BusParam arrays.
- Makefile header-dependency gap (variable definitions must sit above first use in
  a prerequisite; see CLAUDE.md "Makefile prerequisite expansion timing").

## Open issues referenced

- #33: Output 3/4 signal swap when two OC apps are loaded (deferred; no code
  mechanism found given per-`.o` isolation).
- #32: evaluate the Expert Sleepers repo for vendorable assets.

## Next steps

1. Spec follow-ups PR (the four items above) to clean the foundation.
2. Pilot-batch brainstorm and spec for the six EASY apps, using the shipped recipe.
3. Preparatory spec for `OC_scale_edit` plus `OC_visualfx` to unlock the MEDIUM
   quantizer cluster.

## Per-entry verification

Three candidates traced end-to-end against vendor source:

- PONGGAME: `APP_PONGGAME.h` has no `SETTINGS_DECLARE` and no `SettingsBase`
  subclass; outputs via `Out(...)` and `ClockOut(...)` only. Confirms the
  zero-settings classification.
- BBGEN: `APP_BBGEN.h` writes `OC::DAC::set(channel, value)` with raw
  `peaks::BouncingBall` output (full-scale codes), and `peaks_bouncing_balls` is
  header-only (no `.cpp` in the vendor tree). Confirms EASY full-scale modulation.
- ENIGMA: `APP_ENIGMA.h` class derives from `HSApplication`, not `OC::App`, and
  calls `ListenForSysEx()` plus EEPROM load on resume. Confirms the out-of-scope
  HSApplication finding.
