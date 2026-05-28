# O_C apps foundation implementation plan

> For agentic workers: REQUIRED SUB-SKILL. Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task by task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

Goal: build the shared shim foundation that lets vendor O_C full-screen apps
compile and run as independent disting NT plug-ins, validated end to end against
Low-rents and Harrington 1200 with an NT-parameter add-on.

Architecture: a shim layer (graphics extension, hand-ported menu widgets, I/O
accessors, app lifecycle, control router, settings or JSON persistence) drives
each vendor `OC::App` thunk table from the NT `step` / `draw` / `customUi`
entry points. One app per plug-in, mirroring the per-applet model.

Tech stack: C++ (arm-none-eabi-c++ for the NT target, clang++/g++ host), Catch2
host tests via the `nt_runtime` simulator, Make.

Source of truth: the design spec at
`docs/superpowers/specs/2026-05-27-oc-apps-foundation-design.md`. Every task
below references a spec section for its contract. Implementers author code from
the spec plus the vendor source at SHA `7800d929`; they do not invent contracts.

Vendor pins: `vendor/O_C-Phazerville` at `7800d929f25868f9a8b7d3d50514532ee001649b`,
`vendor/distingNT_API` at `cd12d876dbe060859828053efab1cbc98c9df251`.

---

## DAG and parallelization strategy

The foundation is tightly interdependent, so Layer 0 is sequential parent work.
Only the two validation apps fan out. This is the honest shape; do not force
parallelism onto Layer 0.

- Layer 0 (sequential, parent): the shim foundation, eight tasks, each landing
  one commit with a green gate between commits to bound blast radius. Tasks 0.1
  through 0.8.
- Layer 1 (parallel, two implementers in worktrees): the Low-rents and
  Harrington 1200 apps, authored from the canonical recipe in the spec. They
  touch disjoint files except the Makefile `OC_APP_LIST` (an append region) and
  are integrated by the parent.
- Layer 2 (sequential, parent): Makefile wiring, integration, `make test` and
  `make arm`, the `.text` and unresolved-symbol gates.
- Layer 3 (verification): hardware smoke after PR open.

End-to-end wallclock target: Layer 0 dominates (sequential foundation). Layer 1
is a two-way fan-out whose wallclock is the slower of the two apps.

### Dependency edges (Layer 0)

- 0.1 graphics extension is a prerequisite for 0.5 menu hand-port.
- 0.2 DAC and 0.3 ADC/DI/config are prerequisites for 0.7 runtime.
- 0.4 strings is a prerequisite for the Harrington 1200 app (Layer 1), not the
  runtime.
- 0.5 menus, 0.6 apps/ui/Arduino, and the I/O tasks are prerequisites for 0.7
  runtime.
- 0.8 harness driver depends on 0.6 (factory shape) and 0.7 (router) for its
  assertions but the `_NT_uiData` synthesizer itself depends only on the NT API.

### Baseline-green rule

Before starting, and after every Layer 0 commit, run `make test` and (for any
task touching a shared header) `make test-applets`. A red baseline halts the
layer. Shared-header tasks (0.1, 0.2) additionally verify byte-identical `.text`
on a sample of existing applet `.o` files (capture the baseline first; see
Task 0.0).

---

## Task 0.0: Baseline capture (parent, before any edit)

Files: none (read-only baseline).

- [ ] Step 1: Confirm worktree and submodules.

Run: `pwd` (expect the `dr+oc-apps-foundation` worktree),
`git rev-parse --abbrev-ref HEAD` (expect `dr/oc-apps-foundation`),
`git ls-tree HEAD vendor/O_C-Phazerville | awk '{print $3}'` (expect
`7800d929f25868f9a8b7d3d50514532ee001649b`). If any mismatch, stop.

- [ ] Step 2: Green baseline.

Run: `make test` and `make test-applets`. Expected: all pass. If red, stop and
report.

- [ ] Step 3: Capture the `.text` baseline for the shared-header regression gate.

Run `make arm` to build the existing applets, then for a sample of five applet
`.o` files (for example `Cumulus`, `Stairs`, `RunglBook`, `ADSREG`,
`ProbabilityDivider`) record the `.text` SHA:

```bash
for a in Cumulus Stairs RunglBook ADSREG ProbabilityDivider; do
  arm-none-eabi-objcopy -O binary --only-section=.text build/arm/$a.o /tmp/$a.text
  shasum -a 256 /tmp/$a.text
done | tee /tmp/oc-foundation-text-baseline.txt
```

Tasks 0.1 and 0.2 re-run this and diff against the baseline.

---

## Layer 0: shim foundation (sequential, parent)

Each task is RED then GREEN then gate then commit. Write the failing host test
first, implement to the spec contract, run the gate, commit.

### Task 0.1: Graphics extension and weegfx compat

Contract: spec section "Graphics extension and weegfx compat" plus the
"Centering mechanism" subsection.

Files:

- Modify: `shim/include/hem_graphics.h` (add the 12 methods and the `weegfx`
  compat namespace).
- Modify: `shim/src/graphics.cpp` (implement the 12 methods against `NT_screen`).
- Test: extend `harness/tests/test_draw_shape.cpp` and
  `harness/tests/test_draw_text.cpp` (or add `test_draw_oc.cpp`).

- [ ] Step 1: Write failing host tests for the new methods. Cover `drawHLine`,
  `drawHLinePattern`, `drawVLine`, `drawVLinePattern`, `print_right`,
  `pretty_print`, `pretty_print_right`, `movePrintPos`, `write_right`,
  `drawStr`, `writeBitmap8`, `drawAlignedByte`. Assert pixel and print-position
  effects on `NT_screen` for the simplest case of each. Add the `weegfx` compat
  namespace (`coord_t`, `kFixedFontW = 6`, `kFixedFontH = 8`, `PIXEL_OP`,
  `CLEAR_FRAME`) and a compile assertion that `weegfx::kFixedFontW == 6`.
- [ ] Step 2: Run the new tests, confirm they fail.
- [ ] Step 3: Implement the methods and the compat namespace. Match vendor
  `weegfx.h` semantics (the methods are additive; do not change existing ones).
- [ ] Step 4: Run `make test-draw-shape test-draw-text` (and the new binary).
  Confirm pass.
- [ ] Step 5: Shared-header gate. Run `make test-applets` (expect green) and
  re-run the `.text` sample SHA check from Task 0.0 Step 3. The additive methods
  must leave existing applet `.text` byte-identical; if any SHA differs,
  investigate before proceeding (an existing method changed inadvertently).
- [ ] Step 6: Commit. `git commit -m "feat(shim): extend Graphics with weegfx
  full-screen methods (#29)"`.

### Task 0.2: DAC channel representation and accessors

Contract: spec section "I/O accessors" (the DAC bullet and the channel
representation paragraph) plus the "Shared-header regression" risk.

Files:

- Modify: `shim/include/OC_DAC.h` (change `enum DAC_CHANNEL` to `using
  DAC_CHANNEL = int;` plus `extern` channel objects, keep `DAC_CHANNEL_LAST`;
  add `set` and `set<channel>`, `set_pitch`, `semitone_to_dac`, `pitch_to_dac`,
  `set_octave`, `value`, `Update`, `get_voltage_scaling`,
  `set_voltage_scaled_semitone<channel>`, `OutputVoltageScaling`, `getHistory`,
  `kHistoryDepth`).
- Modify: `shim/src/globals.cpp` (define the `DAC_CHANNEL_*` extern objects and
  the per-channel output-history ring; back `set`/`value` with `NT_screen`-style
  output state, history pushed on each `set`).
- Test: add `harness/tests/test_dep_oc_dac.cpp` (or fold into a new
  `test_oc_io.cpp`).

- [ ] Step 1: Write failing host tests: `set<DAC_CHANNEL_A>(v)` then `value(0)`
  round-trips; `getHistory` returns the last `kHistoryDepth` pushes;
  `get_voltage_scaling` returns `VOLTAGE_SCALING_1V_PER_OCT`;
  `set_voltage_scaled_semitone` matches the standard semitone path. Assert
  `DAC_CHANNEL_A` is usable as a template reference argument (the test calling
  `set<DAC_CHANNEL_A>` is itself the compile proof).
- [ ] Step 2: Run, confirm fail (or compile-fail on the template form against
  the old enum, which is the point).
- [ ] Step 3: Implement the representation change and methods.
- [ ] Step 4: Run the new test, confirm pass.
- [ ] Step 5: Shared-header gate. `make test-applets` must stay green and the
  `.text` sample SHAs must match the Task 0.0 baseline (the representation
  change is runtime-equivalent for existing value uses). If `.text` differs but
  tests pass, record the diff and confirm it is only the representation change,
  not behavior; surface to the user if any applet test regresses.
- [ ] Step 6: Commit. `git commit -m "feat(shim): vendor DAC channel
  representation, history, and full accessors (#29)"`.

### Task 0.3: ADC, digital inputs, and config constants

Contract: spec section "I/O accessors" (ADC and digital-inputs bullets) and
"Vendor strings and constants" (the `OC_config.h` bullet).

Files:

- Create: `shim/include/OC_ADC.h`, `shim/include/OC_digital_inputs.h`,
  `shim/include/OC_config.h`.
- Modify: `shim/src/globals.cpp` (or a new `shim/src/oc/io.cpp`) for the
  `ADC_CHANNEL_*` extern objects and the input or trigger backing.
- Test: extend `test_oc_io.cpp`.

- [ ] Step 1: Write failing host tests: `OC::ADC::value<ADC_CHANNEL_1>()` and
  `value(channel)` read the injected NT input; `pitch_value` applies 1V/oct
  scaling; `raw_pitch_value` is present; `OC::DigitalInputs::clocked(DI_1)` and
  the template form report a rising edge after a bus edge then clear after
  `Scan()`; `OC_config.h` exposes `OC_CORE_ISR_FREQ == 16666` and
  `kMaxTriggerDelayTicks == 96`.
- [ ] Step 2: Run, confirm fail.
- [ ] Step 3: Implement. Leave `ARDUINO_TEENSY41` and `__IMXRT1062__` undefined
  so the four-channel branches compile.
- [ ] Step 4: Run, confirm pass. `make test` green.
- [ ] Step 5: Commit. `git commit -m "feat(shim): O_C ADC, digital-input, and
  config-constant accessors (#29)"`.

### Task 0.4: OC_strings extension

Contract: spec section "Vendor strings and constants" (the `OC_strings.h`
bullet).

Files:

- Modify: `shim/include/OC_strings.h` (declare `cv_input_names_none`,
  `trigger_delay_times`, `trigger_delay_ticks`, `kNumDelayTimes = 8`).
- Modify: `Makefile` (add the `build/arm/vendor_src/OC_strings.o` rule usage;
  it already matches the generic `vendor_src/%.o` rule). Add `OC_strings.cpp` to
  the host-test vendor dep set where the Harrington 1200 host test needs it.
- Test: a host test that references each new table compiles and links against
  the vendor `OC_strings.cpp`.

- [ ] Step 1: Write a failing host test that prints one entry of each new table.
- [ ] Step 2: Run, confirm fail (undeclared).
- [ ] Step 3: Add the declarations; wire `OC_strings.cpp` into the test link.
- [ ] Step 4: Run, confirm pass.
- [ ] Step 5: Commit. `git commit -m "feat(shim): declare OC_strings tables for
  the app catalog (#29)"`.

### Task 0.5: Menu widgets hand-port

Contract: spec section "Menu widgets" plus the "Key design decision" section
(why this is a hand-port, not a vendor compile).

Files:

- Create: `shim/include/OC_menus.h` (hand-ported `menu::ScreenCursor`,
  `SettingsList`, `SettingsListItem`, `TitleBar`, `DefaultTitleBar`,
  `DualTitleBar`, the layout constants, `vectorscope_render`,
  `visualize_pitch_classes`), `shim/include/OC_bitmaps.h` (stub).
- Create: `shim/src/oc/menus.cpp` (any non-header-only widget impl, the
  scope-read helper, `visualize_pitch_classes`).
- Test: `harness/tests/test_oc_menus.cpp`.

- [ ] Step 1: Write failing host tests: a `SettingsList` over a small
  `SettingsBase` draws the expected lines; `ScreenCursor` scroll and edit toggle
  behave; `DualTitleBar` and `DefaultTitleBar` draw column headers;
  `vectorscope_render` reads `DAC::getHistory` and plots without overrun;
  `visualize_pitch_classes` draws a circle. Assert against `NT_screen`.
- [ ] Step 2: Run, confirm fail.
- [ ] Step 3: Hand-port the widgets onto `shim::Graphics`. Port the vendor logic
  faithfully; do not pull vendor `OC_menus.h` or `display.h`.
- [ ] Step 4: Run, confirm pass. `make test` green.
- [ ] Step 5: Commit. `git commit -m "feat(shim): hand-port OC_menus widgets on
  shim Graphics (#29)"`.

### Task 0.6: App framework, UI control enum, FASTRUN

Contract: spec sections "App lifecycle and dispatch" and "Control router" plus
the `FASTRUN` note in "Centering mechanism".

Files:

- Create: `shim/include/OC_apps.h` (the `OC::App` struct verbatim, the `OC::apps`
  namespace, `AppEvent`), `shim/include/OC_ui.h` (the `UiControl` enum in
  `namespace OC`, default bit assignment).
- Modify: `shim/include/Arduino.h` (add `#define FASTRUN`).
- Test: `harness/tests/test_oc_apps.cpp` (compile-level: the `App` aggregate
  builds from function pointers; `UiControl` values match the spec; `FASTRUN`
  expands to nothing).

- [ ] Step 1: Write failing compile and value tests for the `App` struct field
  set, the `AppEvent` enum values, and the `UiControl` values
  (`CONTROL_BUTTON_L == 1 << 2`, `CONTROL_BUTTON_R == 1 << 3`,
  `CONTROL_ENCODER_L == 1 << 8`, `CONTROL_ENCODER_R == 1 << 9`).
- [ ] Step 2: Run, confirm fail.
- [ ] Step 3: Implement the headers and the `FASTRUN` define.
- [ ] Step 4: Run, confirm pass.
- [ ] Step 5: Commit. `git commit -m "feat(shim): O_C app struct, UiControl enum,
  FASTRUN stub (#29)"`.

### Task 0.7: Per-app runtime

Contract: spec sections "App lifecycle and dispatch", "Control router",
"Lifecycle cadence mapping", "NT-parameter add-on" (including the enum-label
offset rule), "Settings and storage", and the "ARM aggregation" note in the file
layout. This is the largest task; consider a more capable model.

Files:

- Create: `plugins/apps/_per_app_runtime.h` (App aggregate construction from the
  vendor `<PREFIX>_*` thunks per the `DECLARE_APP` field order; NT entry glue;
  control router with edge detection, long-press timing, live `.mask`; isr
  cadence accumulator that increments `OC::CORE::ticks`, refreshes `OC::ADC`,
  runs `DigitalInputs::Scan()` with one-edge-per-tick discipline, and flushes
  `OC::DAC`; `loop()` per `draw()`; screensaver select with idle counter; the
  per-row 32-byte centering shift; the NT-parameter add-on with the
  construct-time sentinel, `NT_setParameterFromUi` push, and the
  `value_names + min` enum offset; `serialise`/`deserialise` wrapping
  `SettingsBase::Save`/`Restore`).
- Create: the OC aggregation header (for example
  `shim/include/oc_shim_impl.h`) that `#include`s the shim `.cpp` bodies the
  apps need (globals, graphics, icons, cxx_runtime_stubs, the new `oc/*.cpp`,
  quant, cv_map/bjorklund), mirroring `hem_shim_impl.h:4-14` but without the
  Hemisphere-coupled headers.
- Test: covered by the Layer 1 app tests and the Task 0.8 router test; add a
  focused `harness/tests/test_oc_runtime.cpp` for the cadence accumulator (isr
  call count per buffer holds the 16.666 kHz average; one trigger edge is one
  tick) and the centering shift (a pixel drawn at vendor x maps to NT x + 64).

- [ ] Step 1: Write failing tests for the cadence accumulator and the centering
  shift against the `nt_runtime` simulator.
- [ ] Step 2: Run, confirm fail.
- [ ] Step 3: Implement the runtime and the aggregation header.
- [ ] Step 4: Run, confirm pass. `make test` green.
- [ ] Step 5: Commit. `git commit -m "feat(apps): per-app runtime, router,
  cadence, param add-on (#29)"`.

### Task 0.8: Harness control-event driver and router unit test

Contract: spec section "Validation plan" (the `customUi` test idiom and the
router unit test bullets).

Files:

- Create: a `_NT_uiData` synthesis helper in `harness` (build a snapshot, emit
  button down or up and long-press edge sequences, set encoder deltas).
- Create: `harness/tests/test_oc_router.cpp` (foundation-level).
- Modify: `Makefile` (host-test rule for the router test).

- [ ] Step 1: Write the router test: short press emits `EVENT_BUTTON_PRESS`;
  held past 500 ms emits `EVENT_BUTTON_LONG_PRESS` then
  `EVENT_BUTTON_LONG_RELEASE`; encoder deltas emit `EVENT_ENCODER`; a chord sets
  `.mask` to the live `controls` bitmask. Drive the router directly through the
  synthesized `_NT_uiData`.
- [ ] Step 2: Run, confirm fail.
- [ ] Step 3: Implement the helper; wire the test rule.
- [ ] Step 4: Run, confirm pass.
- [ ] Step 5: Commit. `git commit -m "test(harness): _NT_uiData driver and O_C
  router unit test (#29)"`.

---

## Layer 1: validation apps (parallel, two implementers)

Both apps follow the canonical recipe in the spec section "Canonical recipe:
porting one O_C app". Dispatch the two implementers in a single message, each in
its own worktree branched from the feature branch head AFTER Layer 0 commits.

Per-implementer allowed surface:

- Low-rents: `plugins/apps/Low_rents.cpp`,
  `shim/include/oc_app_manifests/Low_rents.h`,
  `harness/tests/test_oc_app_Low_rents.cpp`, and its own `OC_APP_LIST` and
  `VENDOR_DEPS_Low_rents` lines (append region) in the Makefile.
- Harrington 1200: `plugins/apps/Harrington1200.cpp`,
  `shim/include/oc_app_manifests/Harrington1200.h`,
  `harness/tests/test_oc_app_Harrington1200.cpp`, and its own `OC_APP_LIST` and
  `VENDOR_DEPS_Harrington1200 := build/arm/vendor_src/OC_strings.o` lines
  (append region) in the Makefile.

### Task 1.1: Low-rents app

Contract: spec per-app entry "Low-rents" and the canonical recipe.

- [ ] Step 1: Write the failing per-app test
  (`harness/tests/test_oc_app_Low_rents.cpp`): settings round-trip over the 10
  fields; isr produces the expected DAC output for a known frequency and rho;
  the reset trigger fires exactly once per edge under the accumulator; param
  add-on bidirectional sync.
- [ ] Step 2: Run, confirm fail.
- [ ] Step 3: Author `Low_rents.cpp` and the manifest from the recipe. Construct
  the `OC::App` aggregate from the `LORENZ_*` thunks. `VENDOR_DEPS_Low_rents`
  reuses the existing `streams_*.o`.
- [ ] Step 4: Run `make build/host/test_oc_app_Low_rents` and the test. Confirm
  pass.
- [ ] Step 5: Build `make build/arm/Low_rents.o`. Verify `.text` with
  `arm-none-eabi-readelf -W -S` and no unexpected unresolved symbols with
  `arm-none-eabi-nm build/arm/Low_rents.o | grep ' U '`.
- [ ] Step 6: Commit on the implementer branch. Report worktree path, branch,
  SHA, test output, `.text` size.

### Task 1.2: Harrington 1200 app

Contract: spec per-app entry "Harrington 1200" and the canonical recipe.

- [ ] Step 1: Write the failing per-app test: settings round-trip over the 37
  fields; tonnetz transform output for known root and transform inputs; the four
  transform triggers handled; the circle screensaver draws without faulting;
  param add-on bidirectional sync.
- [ ] Step 2: Run, confirm fail.
- [ ] Step 3: Author `Harrington1200.cpp` and the manifest from the recipe.
  Construct the `OC::App` aggregate from the `H1200_*` thunks.
  `VENDOR_DEPS_Harrington1200` is `build/arm/vendor_src/OC_strings.o`; bjorklund
  rides the aggregation header.
- [ ] Step 4: Run `make build/host/test_oc_app_Harrington1200` and the test.
  Confirm pass.
- [ ] Step 5: Build `make build/arm/Harrington1200.o`. Verify `.text` with
  `arm-none-eabi-readelf -W -S` against the roughly 82 KB cap and check
  unresolved symbols. This is the budget canary; if over the cap, apply the
  CLAUDE.md shrink strategies and report.
- [ ] Step 6: Commit on the implementer branch. Report worktree path, branch,
  SHA, test output, `.text` size.

---

## Layer 2: integration (parent)

Files: `Makefile` (add `OC_APP_LIST`, the `BUILD_PER_OC_APP` macro paralleling
`BUILD_PER_APPLET`, the host and ARM test rules; keep `SHIM_CORE_SRCS` and any
new variable defined above its first prerequisite use, per the CLAUDE.md
prerequisite-expansion-timing gotcha).

- [ ] Step 1: Cherry-pick or merge the two implementer commits onto the feature
  branch. Resolve the `OC_APP_LIST` append region by concatenating both entries
  in a stable order.
- [ ] Step 2: Add the `BUILD_PER_OC_APP` macro and the per-app test rules.
- [ ] Step 3: Run `make test` and `make test-applets`. Both green (the
  Hemisphere applets must still pass after the shared-header changes).
- [ ] Step 4: Run `make arm`. Both `build/arm/Low_rents.o` and
  `build/arm/Harrington1200.o` build. Re-run the `.text` and unresolved-symbol
  checks.
- [ ] Step 5: Commit the integration. `git commit -m "feat(apps): wire O_C apps
  foundation and validation apps into the build (#29)"`.
- [ ] Step 6: Open the PR against the default branch (detect it with
  `git remote show origin | grep 'HEAD branch'`). Fill the test plan.

---

## Layer 3: verification (after PR open, hardware)

- [ ] Power cycle the NT before the smoke check (SRAM-size cache rule; the
  algorithm struct is new).
- [ ] Deploy and smoke-check both apps per the spec "Validation plan" hardware
  list: centered render at x-offset 64, buttons 3 and 4 as Up and Down, encoder
  and encoder-push navigation, screensaver entry and exit, CV in and out through
  the routing params, and a settings save and restore across a preset reload.

---

## Pre-commit hook (parent-installed on each Layer 1 implementer worktree)

Install before dispatching each implementer. The hook is parent-owned, not
implementer-authored.

```bash
#!/usr/bin/env bash
# .git/hooks/pre-commit in each implementer worktree
set -euo pipefail
branch=$(git rev-parse --abbrev-ref HEAD)
case "$branch" in
  dr/oc-apps-foundation-*) : ;;
  *) echo "reject: branch '$branch' not derived from the feature branch"; exit 1 ;;
esac
# Allowed surface: <APP> is Low_rents or Harrington1200, set per implementer.
allowed_re='^(plugins/apps/<APP>\.cpp|shim/include/oc_app_manifests/<APP>\.h|harness/tests/test_oc_app_<APP>\.cpp|Makefile)$'
staged=$(git diff --cached --name-only)
for f in $staged; do
  if ! [[ "$f" =~ $allowed_re ]]; then
    echo "reject: '$f' outside the implementer allowed surface"; exit 1
  fi
done
# Hard-reject shared-surface files owned by integration.
forbidden_re='^(shim/include/(OC_apps|OC_ui|OC_menus|OC_DAC|OC_ADC|OC_digital_inputs|OC_strings|OC_config|hem_graphics|Arduino)\.h|shim/src/|plugins/apps/_per_app_runtime\.h)'
for f in $staged; do
  if [[ "$f" =~ $forbidden_re ]]; then
    echo "reject: '$f' is integration-owned shared surface"; exit 1
  fi
done
```

The Makefile is in the allowed surface only for the implementer's own
`OC_APP_LIST` and `VENDOR_DEPS_<APP>` append lines; the parent resolves the
append region at integration.

---

## Worktree-dispatch checklist (parent, before Layer 1)

1. Specify the base branch explicitly:
   `git worktree add <path> -b dr/oc-apps-foundation-<app> dr/oc-apps-foundation`.
   Branch from the feature branch head AFTER Layer 0 commits, never from `main`.
2. Verify the spec is reachable in the new worktree:
   `test -f <worktree>/docs/superpowers/specs/2026-05-27-oc-apps-foundation-design.md`.
   Missing means a broken dispatch; abort.
3. Initialize submodules in the new worktree:
   `git -C <worktree> submodule update --init --depth=1 vendor/O_C-Phazerville
   vendor/distingNT_API`. Worktrees do not inherit submodule state.
4. Install the pre-commit hook above with `<APP>` substituted.
5. Recovery for contaminated topology: if an implementer committed outside its
   surface or on the wrong base, discard that branch and re-dispatch; do not
   hand-merge a contaminated branch.

Implementer-side, before reading any path: `pwd` and
`git rev-parse --abbrev-ref HEAD` must match the prompt; `test -f` the spec
path; before committing, `git diff --cached --name-only` must lie inside the
allowed surface.

---

## Abort budget

- During Layer 0 (per task): a task that cannot reach a green gate after two
  attempts halts the layer and surfaces the blocker. A shared-header task (0.1,
  0.2) that regresses `make test-applets` and cannot be made byte-identical or
  test-green halts and surfaces to the user as a scope decision.
- During Layer 1 dispatch: if either implementer aborts substantively, or
  commits outside its allowed surface despite the hook, halt and re-dispatch
  that one. Two substantive aborts halt the layer.
- During Layer 2 integration: any `make test-applets` regression (Hemisphere
  applets red) or `make arm` failure halts integration.
- `.text` budget: if Harrington 1200 exceeds the roughly 82 KB cap and the
  CLAUDE.md shrink strategies do not bring it under, halt and surface; do not
  ship a plug-in that fails to register.
- During verification: a hardware smoke failure surfaces the specific app and
  discrepancy; it does not silently pass.

---

## Reporting

End-of-run message: the commits landed per layer, `make test` and
`make test-applets` results, both apps' `.text` sizes versus the cap, any
unresolved-symbol findings, the coverage boundary actually exercised, and any
spec deviation discovered against vendor reality during implementation.

---

## Success criteria

- `make test` and `make test-applets` green (Hemisphere applets unregressed).
- `make arm` builds `Low_rents.o` and `Harrington1200.o`, each under the `.text`
  cap with no unexpected unresolved symbols.
- The router unit test and both per-app tests pass.
- The PR is open against the default branch with a filled test plan.
- Hardware smoke (Layer 3) confirms both apps render centered, navigate with
  buttons 3 and 4, enter and exit the screensaver, and persist settings.
