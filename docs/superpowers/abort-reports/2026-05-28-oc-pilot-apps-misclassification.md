# Abort report: O_C pilot app batch misclassification (issue #36)

Date: 2026-05-28
Status: aborted at audit, before fan-out. Batch re-scoped.
Vendor: `vendor/O_C-Phazerville` pinned at `7800d929` (verified).
Kickoff: `docs/superpowers/prompts/2026-05-28-oc-pilot-apps-kickoff.md`.

## Summary

The kickoff for issue #36 scoped six apps as EASY `OC::App` ports on the O_C apps
foundation. The audit phase traced all six against vendor source. Three of the six
are not `OC::App` apps at all. They subclass `HSApplication`, the Hemisphere
application framework, which the foundation explicitly excludes. The remaining
three are genuine `OC::App` apps but each needs shared-surface Layer 0 work the
kickoff did not budget, and two of the three break the foundation's single
settings-singleton model.

Two abort conditions from the kickoff abort budget (section 22, audit layer)
fired:

1. "a candidate is actually HSApplication" (three of them).
2. "more than 2 of 6 demote out of EASY (halt, reassess the boundary)."

Per the framework, a high deferral rate is evidence the boundary is mis-drawn, not
just that scope is small. The audit halted and surfaced this as a scope decision.
The user chose to halt and re-scope rather than shrink the batch item by item.

## Per-candidate finding (against vendor source)

| App | Base class (actual, vendor source) | Verdict |
| --- | --- | --- |
| FPART | `class Fpart : public settings::SettingsBase<Fpart, FPART_SETTING_LAST>` | true `OC::App`, in scope with Layer 0 |
| BBGEN | `class BouncingBall : public settings::SettingsBase<BouncingBall, BB_SETTING_LAST>` (quad container `bbgen`) | `OC::App`, partial; needs shim DAC additions and a quad-channel facade |
| BYTEBEATGEN | `class ByteBeat : public settings::SettingsBase<ByteBeat, BYTEBEAT_SETTING_LAST>` (quad container `bytebeatgen`) | `OC::App`, partial; same gaps plus a vendor `.cpp` link |
| PONGGAME | `class Pong : public HSApplication` | DEMOTE: not `OC::App` |
| SCALEEDITOR | `class ScaleEditor : public HSApplication, public SystemExclusiveHandler` | DEMOTE: not `OC::App`; sysex; no ENABLE guard |
| THEDARKESTTIMELINE | `class TheDarkestTimeline : public HSApplication, public SystemExclusiveHandler, public settings::SettingsBase<...>` | DEMOTE: not `OC::App`; `<EEPROM.h>`, `src/drivers/display.h`, full `usbMIDI` |

Evidence (direct grep of `vendor/O_C-Phazerville/software/src/`):

- `APP_PONGGAME.h:66`: `class Pong : public HSApplication {`. Includes
  `HSApplication.h`. Output via `Out(...)` / `ClockOut(...)` / `DetentedIn(...)`
  (the Hemisphere bus API), not `OC::DAC`.
- `APP_SCALEEDITOR.h:31`: `class ScaleEditor : public HSApplication, public
  SystemExclusiveHandler {`. Includes `HSApplication.h`, `HSMIDI.h`. No
  `#ifdef ENABLE_APP_*` guard at all.
- `APP_THEDARKESTTIMELINE.h:64-65`: `class TheDarkestTimeline : public
  HSApplication, public SystemExclusiveHandler, public
  settings::SettingsBase<TheDarkestTimeline, DT_SETTING_LAST>`. Hard-includes
  `<EEPROM.h>` (line 28) and `"src/drivers/display.h"` (line 37); uses full
  `usbMIDI` send/receive.
- `APP_FPART.h:190`: `class Fpart : public settings::SettingsBase<Fpart,
  FPART_SETTING_LAST>`. Pitch via `OC::DAC::set_pitch`. Clean `OC::App`.
- `APP_BBGEN.h:63` / `APP_BYTEBEATGEN.h:82`: `SettingsBase` subclasses, but the
  app objects are quad containers (`bbgen`, `bytebeatgen`) holding four embedded
  `SettingsBase` instances, not a single file-scope singleton like
  `lorenz_generator` / `h1200_settings`.

## Root cause

The upstream audit brainstorm
(`docs/superpowers/brainstorms/2026-05-28-oc-apps-port-audit-brainstorm.md`)
categorized PONGGAME, SCALEEDITOR, and THEDARKESTTIMELINE as EASY `OC::App` apps.
Its per-entry verification traced PONGGAME, saw `Out(...)` and `ClockOut(...)`, and
concluded "no `SettingsBase`, zero-settings `OC::App`." That inference was wrong:
`Out` / `ClockOut` are the Hemisphere bus API exposed by `HSApplication`, which is
the tell that the app is Hemisphere, not `OC::App`. The footer traced the right
file and drew the wrong conclusion. The brainstorm did not grep the class
declaration line, so the `: public HSApplication` base went unnoticed.

This is precisely the failure mode the per-entry verification footer exists to
catch (`~/.claude/rules/prompt.md`, "Per-entry verification is mandatory"). It is
recorded here so future audits grep the class declaration explicitly rather than
inferring the base class from the output API.

## Shared-surface gaps for the three real OC::App apps

None of the three is the frictionless "copy the recipe" case the kickoff assumed.

- FPART: 109 settings (`FPART_SETTING_ROOT` through `FPART_SETTING_CHORD98`, plus
  the named head). The runtime caps the parameter table at
  `kMaxSettings = 64` (`plugins/apps/_per_app_runtime.h:90`). FPART needs the cap
  raised past 109. That is a runtime (integration-owned) change, plus a check that
  the `Save()` blob fits `kMaxBlobBytes = 256`.
- BBGEN / BYTEBEATGEN: the shim `OC_DAC.h` provides `set`, `pitch_to_dac`,
  `set_pitch`, and `value`, but not `OC::DAC::get_zero_offset(channel)` or
  `OC::DAC::MAX_VALUE`, which both apps call when writing full-scale modulation
  codes (`APP_BBGEN.h:184`, `APP_BYTEBEATGEN.h:318-320`). BBGEN's screensaver also
  calls `OC::scope_render()` (`APP_BBGEN.h:408`); the shim has only
  `vectorscope_render`. These are bounded shim additions but they are
  integration-owned shared surface, not implementer work.
- BBGEN / BYTEBEATGEN settings model: four embedded `SettingsBase` instances per
  app, not a single file-scope singleton. The foundation's settings facade and
  parameter table assume one `SettingsBase`. Exposing four channels (4 x 11 = 44
  or 4 x 19 = 76 parameter rows) is a facade design decision, not a mechanical
  port. This pushes both apps toward MEDIUM.
- BYTEBEATGEN additionally links `peaks_bytebeat.cpp` (a real vendor `.cpp`, not
  Teensy-coupled), the first `VENDOR_DEPS` link of this set.

## Recommended re-scope

1. Correct the audit brainstorm: move PONGGAME, SCALEEDITOR, and
   THEDARKESTTIMELINE out of the EASY `OC::App` tier into a separate
   HSApplication-apps track (the brainstorm already names ENIGMA, MIDI, and
   NeuralNetwork there). Mark the per-entry verification's PONGGAME conclusion as
   corrected.
2. Re-pick the pilot batch from genuine `OC::App` EASY candidates. The honest EASY
   inventory under the corrected boundary is FPART alone (one clean port,
   needing only the `kMaxSettings` bump). BBGEN and BYTEBEATGEN are MEDIUM: they
   need shim DAC additions plus a quad-channel facade design that belongs in a
   spec, not improvised.
3. Treat the shim DAC additions (`get_zero_offset`, `MAX_VALUE`, `scope_render`)
   and the quad-channel facade as a small preparatory spec, the same way the audit
   already treats `OC_scale_edit` / `OC_visualfx` as preparatory for the MEDIUM
   quantizer cluster.

## Disposition

No ports were attempted. No feature branch for ports was created. This report and
the brainstorm correction are the only deliverables. Issue #36 is re-scoped; the
pilot batch is reduced from six apps to the corrected `OC::App` inventory, with the
three HSApplication apps deferred to the HSApplication track.
