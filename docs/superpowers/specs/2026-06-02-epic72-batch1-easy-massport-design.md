# Epic #72 Batch 1: EASY no-shim Hemisphere applet mass-port (design)

Vendor pin: `7800d929` (O_C-Phazerville). Audit: #71. Epic: #72.

## Scope

Port the 12 EASY applets from #72 Batch 1 to per-applet NT plug-ins. Every
heavy vendor dep these applets pull is already carried by a shipped applet or
shim header (verified in #71), so each is a mechanical instance of the shipped
per-applet recipe with ZERO new shim subsystem work.

Targets: DivSeq, DivSeq10, Palimpsest, SequenceX, TrigSeq, TrigSeq16,
TwoRings, EuclidX, ShiftReg, TB3PO, ProbabilityMelody, CVRecV2.

All 12 are `HemisphereApplet` subclasses with 2 inputs, 2 outputs, and a
non-trivial `OnDataRequest()` (so each gets a pack helper). None needs a
`VENDOR_DEPS_<APPLET>` link (all deps are header-only and shim-baseline or
shipped).

## Canonical recipe (one reference walkthrough)

The shipped `ClockDivider` plug-in is the reference. To port applet `<A>`
whose vendor class is `<A>` at
`vendor/O_C-Phazerville/software/src/applets/<A>.h`:

### 1. Manifest: `shim/include/applet_manifests/<A>.h`

Copy `ClockDivider.h`. Set:

- `guid`: `NT_MULTICHAR('H','m',c2,c3)`, unique across the shipped set. Verify
  with `grep -rhoE "NT_MULTICHAR\('H','m'" shim/include/applet_manifests/`.
  The `static_assert` only enforces the `Hm` prefix; uniqueness is the
  implementer's check.
- `name`: human label, <= ~14 chars (vendor `applet_name()`).
- `description`: one short sentence.
- `inputs[]`: two `BusParam`. Name from the vendor `SetHelp()`
  `HELP_DIGITAL1/2` (or `HELP_CV1/2` when the input is primarily a CV).
  `BusKind::gate` for clock/reset/trigger/gate inputs; `BusKind::cv` for
  pitch/CV inputs.
- `outputs[]`: two `BusParam`. Name from `HELP_OUT1/2`. `BusKind::gate` for
  trigger/gate outputs; `BusKind::cv` for pitch/CV outputs.

The `// Vendor deps:` comment line records "(none)".

### 2. Plug-in: `plugins/applets/<A>.cpp`

Copy `ClockDivider.cpp` verbatim and change only:

- The four `#include` lines: manifest path and vendor applet path become `<A>`.
- `using Manifest = per_applet::<A>;`
- `using <A>App = <A>;` only if a name clash forces an alias (ClockDivider
  aliased to avoid shadowing; most do not need it, but the instance member is
  `<A> applet;` so a clash with `Manifest` cannot occur because Manifest is in
  `per_applet::`). Keep the instance member type the vendor class name.
- `kNumParams = input_count + 2 * output_count` (here 2 + 2*2 = 6 for all 12).
- Encoder/button routing: `on_encoder_turn_impl` -> `OnEncoderMove(dir)`,
  `on_button_press_impl` -> `OnButtonPress()`. `on_aux_button_impl`: call the
  vendor aux action ONLY if the vendor applet defines one reachable in
  standalone (most are no-ops; see CLAUDE.md "Per-applet standalone customUi
  mapping" -- aux/button1 is NOT claimed in standalone, so leave the body a
  documented no-op unless the applet's encoder-button is its only control).
- Everything else (factory hooks, step/draw/serialise/deserialise, pluginEntry)
  is identical boilerplate from the runtime helpers.

### 3. Test: `harness/tests/test_applet_<A>.cpp`

Copy `test_applet_ClockDivider.cpp` structure. Each test owns a LOCAL
`pack_<a>` helper mirroring the vendor `OnDataRequest()` byte-by-byte (see
CLAUDE.md "Pack helper convention"): `int` fields at the boundary, apply the
vendor bias inside, AND with the field-width mask (not `0xFF`), explicitly zero
any gap bits the vendor `OnDataRequest` skips. Tests:

- `<X>1` factory guid + name.
- `<X>2` construct populates `HemiPluginInterface` magic/version/hooks.
- `<X>3` serialise round-trip preserves the packed fields (verify the local
  pack bit layout, then deserialise -> serialise and compare `hemi_lo`/`hemi_hi`).
- `<X>4`/`<X>5` behavior: drive a clock edge, assert output appears; drive
  reset, assert counter cleared. State the 10x-clocked-multiplier coverage
  shape (CLAUDE.md "Critical gotcha"): SHAPE 2 (round-trip + state injection,
  drop fire-count assertions) is the default for these sequencers; SHAPE 1
  (model the 10x in the math) only if the applet's behavior is purely
  count-based and easy to model.
- `<X>6`-`<X>9` hasCustomUi mask + customUi dispatch (encoder turn, encoder
  button, button1) do not crash and serialise still works.

Tag every case `[per-applet-pilot][<a>]`.

### 4. Makefile

- Add `<A>` to `ALL_APPLET_LIST`.
- Add `VENDOR_DEPS_<A> :=` (empty) in the `VENDOR_DEPS_*` block.

### 5. Icons

Only if the vendor applet references a `PhzIcons::` symbol the shim lacks. Add
a stub to `shim/include/PhzIcons.h` plus `shim/src/icons.cpp`. Most Batch-1
applets draw with text/primitives and need no new icon. Treat a missing-icon
compile error as expected additive work, not a port defect.

### Build + test gate (per applet, in the implementer worktree)

- `make build/host/test_applet_<A>` then `./build/host/test_applet_<A>` green.
- `make build/arm/<A>.o` succeeds; `arm-none-eabi-nm build/arm/<A>.o | grep ' U '`
  shows only the firmware-resolved surface (CLAUDE.md "ARM unresolved-symbol
  surface"); any new unresolved symbol means a missing vendor-dep link or shim
  gap.

## Per-applet worklist

Each implementer authors the manifest + plug-in + test + Makefile entries from
the recipe and the named vendor source. Suggested GUIDs (verify uniqueness):

- DivSeq (`HmDs`): dual divide-sequencer, clkdivmult. In Clock/Reset, out
  Trig 1/Trig 2 (both gate). `OnDataRequest` packs the per-step divisions.
- DivSeq10 (`HmDt`): single-channel DivSeq variant, clkdivmult. In Clock/Reset,
  out Trig 1/Trig 2 (gate).
- Palimpsest (`HmPm`): accent-array sequencer, no deps. In Clock/Brush (gate),
  out Output (cv)/Trigger (gate).
- SequenceX (`HmSx`): 8-step CV sequencer, no deps. In Clock/Reset (gate), out
  CV (cv)/Step 1 (gate).
- TrigSeq (`HmTs`): 2x8 trigger sequencer, no deps. In Clock/Reset (gate), out
  Trg Ch1/Trg Ch2 (gate).
- TrigSeq16 (`HmT6`): 16-step trigger sequencer, no deps. In Clock/Reset (gate),
  out Trg/Inverse (gate).
- TwoRings (`Hm2R`): dual resonator/quantizer, QuantizerLookup (have). In
  Clock/p Gate (gate), outputs are mode-dependent (cv default).
- EuclidX (`HmEx`): dual euclidean, bjorklund (shim core). In Clock/Reset
  (gate), out Chan1/Chan2 (gate).
- ShiftReg (`HmSR`): shift-register quantizer, braids (have). In Clock/p Gate
  (gate), out 5bits Q (cv)/8bits V (cv).
- TB3PO (`HmT3`): acid-line generator, braids scales (have). In Clock/Regen
  (gate), out Pitch (cv)/Gate (gate). ~400 B regen state in instance SRAM.
- ProbabilityMelody (`HmPM`): probabilistic melody, HSProbLoopLinker (have). In
  Clock 1/Clock 2 (gate), out Pitch 1/Pitch 2 (cv).
- CVRecV2 (`HmCv`): dual CV recorder, SegmentDisplay (have). In Clock/Reset
  (gate), out Play 1/Play 2 (cv). ~1.5 KB heap-free state in instance SRAM.

## Spec footer

### Recipe spot-check

The recipe is the verbatim shipped `ClockDivider` plug-in plus the documented
per-applet substitutions. 55 applets already shipped through this exact recipe;
the runtime helpers (`emit_base_parameters`, `populate_frame_from_bus`,
`run_controller_inner_ticks`, `write_outputs_to_bus`, `route_custom_ui`,
`write_data_request`, `read_data_receive`) are unchanged and carry all
boilerplate.

### Per-entry verification (3 of 12 traced against vendor `7800d929`)

- DivSeq (`applets/DivSeq.h`): class `DivSeq : public HemisphereApplet` (line
  26), `SetHelp` HELP_DIGITAL1="Clock", HELP_DIGITAL2="Reset",
  HELP_OUT1="Trig 1", HELP_OUT2="Trig 2" (lines 190-195). Has `OnDataReceive`
  (line 170) so `OnDataRequest` exists -> pack helper required. Manifest in
  Clock/Reset gate, out Trig 1/Trig 2 gate. CONSISTENT.
- EuclidX (`applets/EuclidX.h`): class `EuclidX : public HemisphereApplet`
  (line 36), HELP_DIGITAL1="Clock", HELP_DIGITAL2="Reset", HELP_OUT1="Chan1",
  HELP_OUT2="Chan2" (lines 219-224). `OnDataReceive` line 203 -> pack helper.
  Uses `bjorklund` (shim core, no VENDOR_DEPS). CONSISTENT.
- CVRecV2 (`applets/CVRecV2.h`): class `CVRecV2 : public HemisphereApplet`
  (line 27), HELP_DIGITAL1="Clock", HELP_DIGITAL2="Reset", HELP_OUT1="Play 1",
  HELP_OUT2="Play 2" (lines 153-158). `OnDataReceive` line 144 -> pack helper.
  Uses `SegmentDisplay` (shipped by Binary). CONSISTENT.

0 of 3 contradict the source. Recipe derivations hold.

### Shim prereq verification

Per #71, every dep is already carried: clkdivmult (ClockDivider), bjorklund
(shim core `hem_shim_impl.h`), braids quantizer+scales (EnigmaJr/MultiScale/
HSUtils), QuantizerLookup (HSUtils/Pigeons), HSProbLoopLinker
(ProbabilityDivider), SegmentDisplay (Binary). No new shim subsystem. New icon
stubs, if any, are additive per-applet work surfaced at first compile.
