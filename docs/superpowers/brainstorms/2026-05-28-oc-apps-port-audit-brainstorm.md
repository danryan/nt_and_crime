# O_C apps port audit and batch selection

Status: audit complete; categorization corrected 2026-05-28 after a per-entry
source re-trace (issue #36). Three apps first listed as EASY `OC::App` are in fact
`HSApplication` apps and were moved to the HSApplication track. See the abort
report `docs/superpowers/abort-reports/2026-05-28-oc-pilot-apps-misclassification.md`.
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

That leaves 20 nominal candidates. Load-bearing audit finding: six of those 20
are NOT `OC::App` full-screen apps. They inherit `HSApplication` (the Hemisphere
application framework) and depend on Teensy `usbMIDI`, `EEPROM`, or a hard
`#include "src/drivers/display.h"`. They are out of scope for this foundation and
belong to a separate HSApplication track:

- `APP_ENIGMA` (Turing machine; HSApplication, sysex, EEPROM-on-resume).
- `APP_MIDI` (Captain MIDI; HSApplication, Teensy display.h hard-include, usbMIDI,
  EEPROM).
- `APP_NeuralNetwork` (HSApplication, sysex send/receive, EEPROM, 216 settings).
- `APP_PONGGAME` (`class Pong : public HSApplication`; Hemisphere bus I/O). Added
  2026-05-28 after the issue-#36 re-trace.
- `APP_SCALEEDITOR` (`HSApplication + SystemExclusiveHandler`; sysex; no ENABLE
  guard). Added 2026-05-28.
- `APP_THEDARKESTTIMELINE` (`HSApplication + SystemExclusiveHandler +
  SettingsBase`; `<EEPROM.h>`, `src/drivers/display.h`, full `usbMIDI`). Added
  2026-05-28.

True `OC::App` candidate set: 14.

## Categorization (14 OC::App candidates)

EASY means it fits the shipped recipe with no new shared shim subsystem. MEDIUM
means it needs a bounded, reusable shim addition. HARD means a large new
subsystem or a custom full-screen editor beyond the settings-menu model.

| App | Verdict | Output type | Vendor .cpp deps | Key foundation gap |
| --- | --- | --- | --- | --- |
| FPART | EASY | pitch sequencer | none | runtime `kMaxSettings` bump (109 settings > current cap of 64) |
| BBGEN | MEDIUM | full-scale modulation | none | shim `OC::DAC::get_zero_offset`/`MAX_VALUE`/`scope_render`; quad-channel facade (4 embedded SettingsBase) |
| BYTEBEATGEN | MEDIUM | full-scale modulation | peaks_bytebeat | same shim DAC gaps + quad-channel facade; first VENDOR_DEPS link |
| PONGGAME | OUT (HSApplication) | Hemisphere bus (Out/ClockOut) | none | `class Pong : public HSApplication`; belongs to the HSApplication track |
| SCALEEDITOR | OUT (HSApplication) | Hemisphere bus (Out/In) | braids_quantizer, OC_scales | `HSApplication + SystemExclusiveHandler`; sysex; no ENABLE guard |
| THEDARKESTTIMELINE | OUT (HSApplication) | Hemisphere bus + usbMIDI | braids_quantizer, OC_patterns | `HSApplication + SystemExclusiveHandler + SettingsBase`; `<EEPROM.h>`, `src/drivers/display.h` |
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

## Recommended pilot batch (corrected 2026-05-28)

The original recommendation named six EASY `OC::App` apps. The issue-#36 re-trace
found three of them (PONGGAME, SCALEEDITOR, THEDARKESTTIMELINE) are `HSApplication`
apps, not `OC::App` apps, and the other three each need shared-surface work the
EASY label implied was absent. The original list is retained below struck through
for the record; the corrected batch follows.

Original (incorrect) six: PONGGAME, SCALEEDITOR (zero-settings path); BBGEN,
BYTEBEATGEN (full-scale modulation); FPART, THEDARKESTTIMELINE (pitch / pitch +
gate). Three of these were HSApplication apps; the zero-settings and pitch-plus-gate
shapes attributed to them do not apply to this foundation.

Corrected `OC::App` inventory:

- FPART: the only clean EASY `OC::App` port. Pitch sequencer. Needs one runtime
  change: raise `kMaxSettings` past 109 (it has 109 settings; the cap is 64).
- BBGEN, BYTEBEATGEN: MEDIUM, not EASY. Full-scale modulation, but they need shim
  `OC::DAC::get_zero_offset` / `MAX_VALUE` / `OC::scope_render`, and a
  quad-channel settings facade (four embedded `SettingsBase` per app, not the
  single file-scope singleton the foundation assumes). These belong in a small
  preparatory spec, not a mechanical port.

Recommended pilot: FPART alone, or FPART plus a preparatory spec for the BBGEN /
BYTEBEATGEN shim additions and quad-channel facade if the batch is to exercise the
full-scale-modulation shape on more apps.

Defer the MEDIUM tier to a follow-on phase whose Layer 0 builds `OC_scale_edit`
and `OC_visualfx` first (they gate the quantizer cluster). Defer the HARD tier
until the editor subsystems exist. Track the six HSApplication apps (ENIGMA, MIDI,
NeuralNetwork, PONGGAME, SCALEEDITOR, THEDARKESTTIMELINE) under a separate
HSApplication-apps initiative.

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
2. Pilot-batch brainstorm and spec for the corrected `OC::App` inventory (FPART
   clean; BBGEN / BYTEBEATGEN with a preparatory shim spec). The original
   six-EASY-app target was incorrect; see the corrected recommended batch above.
3. Preparatory spec for `OC_scale_edit` plus `OC_visualfx` to unlock the MEDIUM
   quantizer cluster.

## Per-entry verification

Three candidates traced end-to-end against vendor source. The first trace below
was wrong and is corrected here; see the abort report for the full re-trace.

- PONGGAME: CORRECTED. The original trace read `Out(...)` / `ClockOut(...)` and
  concluded "zero-settings `OC::App`." That was wrong. `APP_PONGGAME.h:66` is
  `class Pong : public HSApplication`, and `Out` / `ClockOut` are the Hemisphere
  bus API, not the `OC::App` no-settings path. PONGGAME is an HSApplication app,
  out of scope. The lesson: a per-entry trace must grep the class declaration
  line, not infer the base class from the output API.
- BBGEN: `APP_BBGEN.h:63` is `class BouncingBall : public SettingsBase<...>`, a
  real `OC::App` settings class. It writes full-scale codes via
  `OC::DAC::set(channel, OC::DAC::get_zero_offset(ch) + ...)`, and
  `peaks_bouncing_balls` is header-only. But the app object is a quad container of
  four `BouncingBall` instances, and the shim lacks `get_zero_offset` / `MAX_VALUE`
  / `scope_render`. Corrected verdict: MEDIUM, not EASY.
- ENIGMA: `APP_ENIGMA.h` class derives from `HSApplication`, not `OC::App`, and
  calls `ListenForSysEx()` plus EEPROM load on resume. Confirms the out-of-scope
  HSApplication finding.
