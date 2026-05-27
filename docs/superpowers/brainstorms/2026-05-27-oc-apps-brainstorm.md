# Standalone O_C full-screen apps as independent NT plug-ins: brainstorm

Date: 2026-05-27
Tracks: GitHub issue #29 (foundation)
Vendor pin: `vendor/O_C-Phazerville` at SHA `7800d929f25868f9a8b7d3d50514532ee001649b`

## Goal

Port the O_C (Ornament-and-Crime / Phazerville) full-screen apps (Quantermain,
Enigma, The Darkest Timeline, and roughly twenty others) to the Expert Sleepers
disting NT as independent plug-ins, separate from the existing Hemispheres and
Quadrants applet hosts. Each app becomes its own NT plug-in.

This document captures the investigation behind issue #29. It is reference
material for the eventual per-app port issues. No code has been written.

## Scope decision

Build the shared foundation first. Per-app ports are deferred to their own
issues until the foundation lands and the real per-app cost is known. The
foundation is the substantial work; the apps mostly reuse existing shims once
it exists.

The foundation is built around the NT `hasCustomUi` / `customUi` mechanism so
the vendor app's own UI, draw, and event code runs essentially unmodified.
NT-parameter exposure is an optional per-app add-on, not the primary UI. The
rationale is in the "Settings mapping versus customUI" section below.

## O_C app framework versus Hemisphere applet contract

The current shim targets the Hemisphere applet contract. O_C apps use a
different framework the shim does not provide. The `OC::App` struct (verbatim
from vendor `OC_apps.h`):

```cpp
struct App {
  uint16_t id;
  const char *name;
  void (*Init)();
  size_t (*storageSize)();
  size_t (*Save)(void *);
  size_t (*Restore)(const void *);
  void (*HandleAppEvent)(AppEvent);
  void (*loop)();
  void (*DrawMenu)();
  void (*DrawScreensaver)();
  void (*HandleButtonEvent)(const UI::Event &);
  void (*HandleEncoderEvent)(const UI::Event &);
  void (*isr)();
};
```

| Dimension | O_C app | Hemisphere applet |
| --- | --- | --- |
| Audio | `isr()` free function, timer-ISR rate | `Controller()` virtual, per host buffer |
| Draw | `DrawMenu()` over full 128x64 `graphics` global | `View()` into a half-screen lane via gfx offset/clip |
| Screensaver | `DrawScreensaver()` | none (host owns it) |
| Buttons | `HandleButtonEvent(UI::Event&)`, 4-button enum | `OnButtonPress()` plus `AuxButton()` |
| Encoders | `HandleEncoderEvent(UI::Event&)`, L and R | `OnEncoderMove()`, right encoder only |
| Lifecycle | `HandleAppEvent` SUSPEND/RESUME/SCREENSAVER | none (Start/Reset/Controller/View) |
| Deferred work | `loop()` each main-loop iteration | none |
| Storage | `settings::SettingsBase<T,N>` int array, Save/Restore blob | `OnDataRequest`/`OnDataReceive`, 64-bit blob |
| I/O | 4 CV in, 4 CV out, 4 TR in, 2 encoders, 4 buttons | 2 in, 2 out, 2 clocks per lane, 1 encoder, 1 button per side |
| Registration | compile-time `DECLARE_APP` in `available_apps[]` | runtime `HemiPluginInterface` via `_NT_slot` |

## Foundation scope (issue #29)

- `OC::App` struct plus lifecycle dispatch. Map NT `step()` to
  `current_app->isr()` at audio rate, an NT loop equivalent to `loop()`, and
  suspend/resume/screensaver transitions to `HandleAppEvent`.
- `UI::Event` routing. Translate NT `customUi` events (`kNT_encoderL/R`,
  `kNT_button*`, pots) into O_C `UI::Event` structs (`CONTROL_BUTTON_UP/DOWN/L/R`,
  `CONTROL_ENCODER_L/R`, value). `UI/ui_events.h` compiles as-is; a stub
  `OC_ui.h` exposes only the `UiControl` enum (the firmware `Ui` poll/debounce
  class is replaced by NT dispatch).
- Full-screen `weegfx` extensions beyond the current Hemisphere `Graphics`:
  `print_right`, `pretty_print_right`, `movePrintPos`, `drawHLine`,
  `drawHLinePattern`, `drawAlignedByte`, `kFixedFontW`, and
  `GRAPHICS_BEGIN_FRAME`/`GRAPHICS_END_FRAME` mapped onto `NT_screen`.
- `OC_menus` widgets (`menu::ScreenCursor`, `SettingsList`, `SettingsListItem`,
  `TitleBar`). Header-only once their deps resolve. `OC_bitmaps` needs the
  vendor `.cpp` or a stub.
- Settings and storage. Map `settings::SettingsBase::Save`/`Restore` onto NT
  JSON params (`NT_jsonGet`/`NT_jsonSet`). `util/util_settings.h` compiles
  as-is; the EEPROM `AppData` / `PageStorage` dispatch is replaced.
- Full-app I/O accessors. `OC::DAC::set_pitch`/`value` (4 out),
  `OC::ADC::value`/`pitch_value` (4 in), `OC::DigitalInputs::clocked` (4 TR),
  backed by the NT `inputs[]`/`outputs[]` arrays. `OC::CORE::ticks` is already
  incremented by the Hemisphere shim.
- Screensaver hook and the menu/screensaver redraw cadence.

## Settings mapping versus customUI

O_C `settings::SettingsBase` is an array of typed ints, each with a `value_attr`
(min, max, default, name, optional enum labels). That maps almost 1:1 onto NT
`_NT_parameter` (name, min, max, default, unit, scaling, enum strings), and a
param exposure buys NT CV-to-param mapping, MIDI mapping, and preset save for
free. This works for the flat-settings apps.

It does not cover:

- Conditional param trees. Quantermain's "CV source = Turing" reveals roughly
  eight Turing sub-params; "= scale" hides them. NT param tables are flat and
  static. Showing all sub-params always is cluttered; resizing `numParameters`
  dynamically hits the construct-time `parameterChanged` and SRAM-cache
  invalidation hazards documented in CLAUDE.md.
- Structural editors. The scale/note-mask editor, the sequence step grid, the
  Automatonnetz 5x5 grid, the Neural Net node graph, the Acid Curds chord
  editor. None reduce to a scalar param list.
- Gestures and visualizations. Live left-encoder transpose, hold-Up-as-shift,
  long-press randomize/clear, the pitch-circle, scope, and staff screensavers.
  NT params have no momentary-action or draw concept.

customUI runs the vendor app's actual editors, gestures, and screensavers
unmodified, which is the project's core thesis (compile vendor source against a
shim, do not reimplement). The param-only route would force a per-app UI
rewrite and only fits the six flat-settings apps. Hence customUI is the
foundation; param exposure is an optional per-app add-on where it is cheap.

## Shim reuse map

Already in the shim or header-only, carries over directly:

- `braids_quantizer`, `OC_scales` (quantizer and scales)
- `peaks_*` (bytebeat, multistage envelope, bouncing balls)
- `streams_lorenz_generator` (Low-rents reuses the existing `VENDOR_DEPS_LowerRenz`)
- `bjorklund` (Euclidean), header-only `tonnetz/`, `enigma/`, `neuralnet/`
- graphics core (NT_screen), bus I/O, icons, Arduino/OC stubs
- `util/util_settings.h` compiles as-is

New vendor dependency across the whole catalog: `frames_poly_lfo.cpp`
(Quadraturia) only.

## App inventory and categorization

Source of truth: the `DECLARE_APP` array in vendor `OC_apps.cpp`.

| File | Display name | ID | Category |
| --- | --- | --- | --- |
| APP_HEMISPHERE.h | Hemispheres | HS | Done (shipped host) |
| APP_QUADRANTS.h | Quadrants | QS | Done (shipped host) |
| APP_SETTINGS.h | Setup / About | SE | Out of scope (system) |
| APP_CALIBR8OR.h | Calibr8or | C8 | Out of scope (calibration) |
| APP_SCALEEDITOR.h | Scale Editor | SC | Out of scope (system) |
| APP_WAVEFORMEDITOR.h | Waveform Editor | WA | Out of scope (system) |
| APP_REFS.h | References | RF | Out of scope (calibration) |
| APP_Backup.h | Backup / Restore | BR | Out of scope (system) |
| APP_QQ.h | Quantermain | QQ | Candidate |
| APP_ENIGMA.h | Enigma | EN | Candidate |
| APP_THEDARKESTTIMELINE.h | Darkest Timeline | D2 | Candidate |
| APP_CHORDS.h | Acid Curds | AC | Candidate |
| APP_AUTOMATONNETZ.h | Automatonnetz | AT | Candidate |
| APP_MIDI.h | Captain MIDI | MI | Candidate |
| APP_ASR.h | CopierMaschine | AS | Candidate |
| APP_BBGEN.h | Dialectic Ping Pong | BB | Candidate |
| APP_H1200.h | Harrington 1200 | HA | Candidate |
| APP_LORENZ.h | Low-rents | LR | Candidate |
| APP_DQ.h | Meta-Q | M! | Candidate |
| APP_NeuralNetwork.h | Neural Net | NN | Candidate |
| APP_PASSENCORE.h | Passencore | PQ | Candidate |
| APP_ENVGEN.h | Piqued | EG | Candidate |
| APP_PONGGAME.h | Pong | PO | Candidate |
| APP_POLYLFO.h | Quadraturia | PL | Candidate |
| APP_SCENES.h | Scenery | SX | Candidate |
| APP_SEQ.h | Sequins | SQ | Candidate |
| APP_BYTEBEATGEN.h | Viznutcracker | BY | Candidate |
| APP_FPART.h | 4 Parts | FP | Candidate |

## Per-app digests by port-difficulty tier

Port difficulty reflects UI and control-surface adaptation cost, not DSP. I/O
is the count of CV inputs, CV outputs, and trigger inputs the app uses.

### Low tier

- CopierMaschine (ASR): quantizing analog shift register. 4 CV in (sample,
  index, mask rotate, octave), 4 out (the 4 stages), 4 TR (clock, hold, oct up,
  oct down). Single flat menu plus the shared scale editor. Difficulty: low; the
  256-sample ring-buffer delay is the only subtlety.
- Dialectic Ping Pong (BBGEN): 4-channel bouncing-ball envelope (Peaks). 4 CV
  in (per-channel mappable), 4 out, 4 TR. Flat per-channel menu; 4x4 CV map.
  Difficulty: low.
- Harrington 1200: neo-Riemannian triad transformer. CV1 (root), CV4
  (inversion), 4 TR (reset, P, L, R), 4 out (root plus triad). Short flat menu;
  pitch-circle screensaver. Difficulty: low.
- Low-rents (Lorenz): dual Lorenz/Rossler generators. 4 CV in (freq and rho per
  generator), 4 out (mappable, 20 combos), 4 TR (resets, freeze). Difficulty:
  low; reuses existing `VENDOR_DEPS_LowerRenz`.
- Pong: literal Pong game. CV1 (paddle), outputs A/B triggers, C/D positions.
  Full-screen animation; control need trivial (one axis). Difficulty: low to
  medium (depends on full-screen animation).
- Viznutcracker (bytebeat): 4-channel bytebeat. 4 CV in (per-channel mappable),
  4 out, per-channel trigger. Flat settings list, many loop-point fields.
  Difficulty: low to medium.

### Medium tier

- Quantermain (QQ): 4-channel quantizer with Turing, logistic map, integer
  sequence, bytebeat sources. 4 CV in, 4 out, 4 TR. Interactive scale/note-mask
  editor; conditional per-source sub-params. Difficulty: medium.
- The Darkest Timeline: 2-track CV and probability timeline sequencer with MIDI.
  4 CV in plus CV record, 4 out (2 pitch, 2 gate), 2 TR. Full-screen dual
  timeline bar graph is integral to editing. MIDI in/out. Difficulty: medium.
- Acid Curds (Chords): chord step sequencer, 4 chained 8-step progressions.
  4 CV in (assignable), 4 out (base plus chord notes), 4 TR. Two-level editor
  (menu plus inline chord editor) and a CV-assign sub-menu. Difficulty: medium.
- Automatonnetz: neo-Riemannian vector sequencer on a 5x5 grid. CV1 (root),
  CV4 (inversion), TR1/TR2 (advance, arp). 4 out. 25-cell grid editor with six
  per-cell params. Difficulty: medium.
- Passencore: functional-harmony chord generator. CV1 (tension), CV2 (color),
  CV3/CV4 (constraints), assignable triggers, 4 out. Mostly CV/trigger driven;
  conventional settings list. Difficulty: medium.
- Piqued (EnvGen): 4-channel Peaks envelope with Euclidean gating. 4 CV in
  (per-channel mappable), 4 out, 4 TR. Dual-mode UI with live envelope preview.
  Difficulty: medium.
- Quadraturia (PolyLFO): quadrature wavetable LFO (Frames). 4 CV in (freq,
  shape, spread, mappable), 4 out, 4 TR (reset, freeze, tap, mult/div). Standard
  menu plus waveform preview. New dep `frames_poly_lfo.cpp`. Difficulty: medium.
- Scenery (Scenes): macro CV switcher/crossfader. CV1 (crossfade), CV2 (bias),
  CV3 (slew), CV4 (random seq gate), 4 TR (scene jumps). 4 out. Scene-by-output
  value grid editor plus preset banks. Difficulty: medium.
- 4 Parts (FPart): 4-channel staff-notation chord sequencer. CV1/CV2 (chord
  index/select), 4 TR (step, jump), 4 out. Staff visualization with custom
  glyphs plus a large chord-bank editor. No `_apps` doc; digest from header.
  Difficulty: medium to high.

### High tier

- Enigma: Turing-machine library and song composer, 40 registers, 4-track songs
  up to 396 steps. 4 CV/digital in, 4 out, MIDI plus SysEx for full state. Four
  distinct screens with per-screen encoder and button roles. Difficulty: high.
- Captain MIDI: bidirectional CV/MIDI interface, 18-plus assignment types, four
  switchable setups, MIDI log. 4 CV in, 4 out, 4 TR, full USB MIDI plus SysEx.
  Five sub-screens per setup. Difficulty: high.
- Meta-Q (DQ): 2-channel meta-quantizer with four scale slots per channel.
  CV1/CV3 (sample), CV2/CV4 (aux), TR1-4. 4 out. Scale editor with a hold-Up-as-
  shift chord gesture. Difficulty: high.
- Neural Net: six-neuron logic gate network, 11 gate types, four setups. 8
  inputs (4 TR plus 4 CV as boolean), 4 out, MIDI SysEx. Node-graph
  visualization edited by cursor traversal. Difficulty: high.
- Sequins (Seq): 2-channel sequencer, four 16-step sequences per channel, many
  play modes and directions, per-step envelopes. 4 CV in (mappable), 4 out, 4
  TR. Three UI surfaces including a step editor with chorded fine-tune and
  copy/paste. Difficulty: high.

## Risks and open questions for the foundation brainstorm

- Control-surface mapping. O_C has 4 buttons (Up/Down/L/R) plus 2 encoders; NT
  has a different surface. Chorded gestures (hold-Up-as-shift in Meta-Q and
  Sequins, long-press randomize/clear) need a deliberate mapping.
- Screen size. O_C apps draw to 128x64; NT is 256x64. Center, or use the width.
- `.text` budget. Full apps are larger than applets. The firmware refuses a
  plug-in whose `.text` exceeds roughly 82 KB (see CLAUDE.md). The heaviest
  apps (Sequins around 2649 lines, Quantermain around 1620) are the risk; each
  ported app must be verified to fit.
- Conditional NT params. If an app also exposes NT params, dynamic
  `numParameters` hits the construct-time `parameterChanged` and SRAM-cache
  hazards documented in CLAUDE.md.

## Validation

Prove the foundation end-to-end with one low-tier app as the acceptance test
(Harrington 1200 or Low-rents). This validating app is part of the foundation
issue, not a separate per-app port.

## Out of scope

- Per-app ports beyond the one validating app (separate issues, after the
  foundation lands).
- System and utility apps: Setup/About, Calibr8or, Scale Editor, Waveform
  Editor, References, Backup.
- Hemispheres and Quadrants (already shipped as composer hosts).

## Status and next steps

Foundation tracked as issue #29 (foundation-only for now). When the foundation
is picked up it gets its own brainstorm, spec, and plan under
`docs/superpowers/`. Per-app port issues are spun out from the tiered catalog
above once the foundation lands and the real per-app cost is measured.
