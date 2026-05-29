# Kickoff: O_C pilot app-port batch (6 EASY apps)

## 1. Opening

The O_C full-screen apps foundation shipped in PR #31 (issue #29, merged): the
`OC::App` runtime (`plugins/apps/_per_app_runtime.h`), the 16-bit DAC output model,
the menu/graphics hand-port, the settings-to-`_NT_parameter` add-on, and the build
model (`BUILD_PER_OC_APP`, `OC_APP_LIST`). Two reference apps are live and
hardware-verified: Low-rents (`plugins/apps/Low_rents.cpp`) and Harrington 1200
(`plugins/apps/Harrington1200.cpp`). The portability audit
(`docs/superpowers/brainstorms/2026-05-28-oc-apps-port-audit-brainstorm.md`)
categorized the catalog and selected a pilot batch.

This phase ports the six EASY `OC::App` apps that need no new shared shim
subsystem. It validates the recipe at scale and exercises three shapes the two
shipped apps did not: the zero-settings path, full-scale modulation outputs, and
pitch/gate sequencing. It does NOT build the `OC_scale_edit`/`OC_visualfx`
subsystems (those gate the MEDIUM quantizer cluster, a later phase).

## 2. Two or three most important rules

- Parallelize. The six ports are independent; dispatch them as parallel
  implementer subagents in isolated worktrees, one app each, in a single message.
  End-to-end wallclock targets the slowest single port plus integration, not the
  sum. See `~/.claude/rules/parallel-execution.md`.
- Implementer worktrees branch from the feature branch head AFTER Layer 0, never
  from `main`. The spec/plan live on the feature branch; a worktree off `main`
  fails spec discovery silently (the Phase 3 attempt-1 failure mode).
- Passing host tests and clean registration do NOT prove a plug-in ADDs. Verify
  every app ADDs on hardware via `mcp__nt_helper__new` with its GUID, not just that
  it registers. The firmware add path (serialise, sampleRate-driven step) is
  unmodeled by the harness.

## 3. Model selection

- Parent orchestrator: Opus.
- Implementer subagents: Sonnet (each port is mechanical against a frozen recipe).
- Audit and review subagents: Opus for the per-entry spec audit and the final
  review; Sonnet for per-task spec/quality reviews.

## 4. Autonomous execution contract

Run: audit -> single preflight checkpoint -> brainstorm -> spec -> plan ->
parallel fan-out -> integration -> verification. One preflight checkpoint only,
after the audit and before the brainstorm. Proceed automatically unless an abort
condition fired. No other review gates block autonomous execution.

## 5. Branch context

- Feature branch: `dr/oc-pilot-apps`, created from `main` after PR #34 merges (the
  audit, the CLAUDE.md learnings, and this kickoff are then on `main`).
- Parent worktree: `.claude/worktrees/dr-oc-pilot-apps`.
- After `git worktree add` (or `EnterWorktree`), run
  `git submodule update --init --recursive --depth=1` in the new worktree before
  any build; worktrees do not inherit submodule state. Implementers read vendor
  app sources, so they need the `vendor/O_C-Phazerville` submodule initialized.

## 6. Worktree naming

- Parent: `.claude/worktrees/dr-oc-pilot-apps` on `dr/oc-pilot-apps`.
- Implementer: `.claude/worktrees/dr-oc-pilot-<app>` on `dr/oc-pilot-<app>`,
  branched from `dr/oc-pilot-apps` head after Layer 0.

## 7. Pre-commit hook (parent-installed on each implementer worktree)

- Branch pattern: reject commits on branches not matching `dr/oc-pilot-*`.
- Allowed surface per implementer: only `plugins/apps/<APP>.cpp`,
  `shim/include/oc_app_manifests/<APP>.h`, `harness/tests/test_oc_app_<APP>.cpp`,
  and that app's own `OC_APP_LIST`/`VENDOR_DEPS_<APP>` lines in the Makefile (the
  Makefile edit is append-only within the app's marker; prefer integration owning
  the `OC_APP_LIST` line, see below).
- Hard-reject (integration-owned shared surface): `plugins/apps/_per_app_runtime.h`,
  `shim/include/oc_shim_impl.h`, `shim/include/OC_DAC.h`, `shim/include/OC_menus.h`,
  any other `shim/` core file, and the `harness/` infrastructure
  (`nt_runtime.*`, `plugin_loader.*`, `catch_main.cpp`). If a port needs a shared
  change, the implementer reports it as a blocker; integration makes it in Layer 0
  or Layer 2, never the implementer.

## 8. Required skills and rules

`superpowers:brainstorming`, `superpowers:writing-plans`,
`superpowers:subagent-driven-development`, `superpowers:using-git-worktrees`,
`superpowers:test-driven-development`, `superpowers:verification-before-completion`.
Personal rules: `~/.claude/rules/parallel-execution.md`, `~/.claude/rules/prompt.md`,
`~/.claude/rules/worktrees.md`, `~/.claude/rules/git.md`. Project: the repo
`CLAUDE.md` (O_C apps section, the add-time hazards, the per-`.o` isolation, the
nt_helper deploy gotchas).

## 9. Frozen recipes

Do not redesign these; the brainstorm selects against them, the spec references the
class, implementers author from them plus vendor source.

Port recipe (per app), mirroring `Low_rents.cpp`/`Harrington1200.cpp`:

- A per-app `.cpp` defining `NT_OC_APP_TU 1` (aggregation), building the `OC::App`
  from the vendor `<PREFIX>_*` static thunks, the `_NT_factory` in vendor field
  order, and the customUI emit-glue (`::UI::Event` constructed from runtime
  primitives, bridged to the App handlers by `reinterpret_cast`).
- A manifest `shim/include/oc_app_manifests/<APP>.h`.
- `OC_APP_LIST += <APP>` plus `VENDOR_DEPS_<APP>` (only the vendor `.cpp` the app
  links; empty if none) and `VENDOR_DEP_HOST_SRCS_<APP>` for the host test.
- A per-app GUID (`OC??`), unique across the registered set.

Test recipe (per app), mirroring `test_oc_app_Low_rents.cpp`/`_Harrington1200.cpp`:

- Loads through the factory path with a custom UI.
- `draw()` renders (non-zero screen).
- Settings round-trip through factory serialise/deserialise (skip if the app has
  zero settings; PONGGAME and SCALEEDITOR do).
- NT-parameter add-on bidirectional sync, INCLUDING the
  `nt::set_parameter_offset(1)` regression for the customUI push-back (skip if zero
  settings).
- Output assertion sized to the app's output type: pitch apps assert 1V/oct on the
  routed bus; full-scale modulation apps assert the output stays within +-5V (not
  railed) and moves; gate apps assert one edge per event. The tick accumulator runs
  `isr()` many times per `step()`; model that in cadence/edge math or cover via
  state injection (the OC analog of the Hemisphere 10x-clock rule).

## 10. Operational boundary (in-scope predicate)

An app is in scope iff: it subclasses `OC::App` (NOT `HSApplication`); the audit
marked it EASY; it needs no new shared shim subsystem (no `OC_scale_edit`,
`OC_visualfx`, chord/input-map editors); its vendor `.cpp` deps are already
buildable or header-only; and it fits one implementer subagent.

## 11. Scope candidates (6)

From the audit. Per-app one-liner names vendor source, output type, settings, deps,
and the structural test focus.

- PONGGAME (`APP_PONGGAME.h`): modulation + gates, ZERO settings (tests the
  `numParameters == kIoParamCount` path), no vendor `.cpp`. Focus: no-settings
  factory + gate/CV output; no serialise round-trip test.
- FPART (`APP_FPART.h`): pitch sequencer, ~99 settings (chord library), no vendor
  `.cpp` (all headers shadowed). Focus: large settings table round-trip; pitch
  output 1V/oct.
- BBGEN (`APP_BBGEN.h`): full-scale modulation (bouncing-ball physics), 12 settings,
  `peaks_bouncing_balls` header-only (no `.cpp`). Focus: full-scale output within
  +-5V; gate-triggered retrigger.
- BYTEBEATGEN (`APP_BYTEBEATGEN.h`): full-scale modulation, 20 settings, links
  `peaks_bytebeat.cpp` (`VENDOR_DEPS`). Focus: full-scale output; conditional menu
  items (`num_enabled_settings`/`enabled_settings[]`).
- SCALEEDITOR (`APP_SCALEEDITOR.h`): pitch only, ZERO `SettingsBase` settings (edits
  the `OC::user_scales` global), links `braids_quantizer.cpp` + `OC_scales.cpp`,
  uses `SegmentDisplay` (confirm the shim already provides it). Focus: no-settings
  path; custom grid UI renders; pitch output.
- THEDARKESTTIMELINE (`APP_THEDARKESTTIMELINE.h`): pitch + gates, 8 settings, links
  `braids_quantizer.cpp` + `OC_patterns.cpp` (+ `OC_scales.cpp`). Focus: small
  settings round-trip; pitch+gate output; MIDI out is native, no shim delta.

## 12. Out of scope

The MEDIUM tier (ASR, AUTOMATONNETZ, DQ, ENVGEN, PASSENCORE, POLYLFO, SCENES,
WAVEFORMEDITOR) and the HARD tier (CHORDS, QQ, SEQ). The `OC_scale_edit` /
`OC_visualfx` subsystems. The HSApplication apps (ENIGMA, MIDI, NeuralNetwork). The
deferred #33 Output 3/4 swap.

## 13. Vendor and dependency pins

- `vendor/O_C-Phazerville` at `7800d929` (verify
  `git ls-tree HEAD vendor/O_C-Phazerville`).
- `vendor/distingNT_API` (NT plug-in ABI).
- `vendor/llvm-project` at `llvmorg-19.1.0`, sparse-checkout
  `compiler-rt/lib/builtins` (provisioned by `bootstrap.sh` / `make vendor`).

## 14. Primary references (read in full during audit)

- `docs/superpowers/brainstorms/2026-05-28-oc-apps-port-audit-brainstorm.md` (this
  batch's source).
- `docs/superpowers/specs/2026-05-27-oc-apps-foundation-design.md` (the recipe + its
  Implementation notes).
- `CLAUDE.md` O_C apps section, add-time hazards, per-`.o` isolation, nt_helper
  deploy gotchas.
- `plugins/apps/Low_rents.cpp`, `plugins/apps/Harrington1200.cpp`, and their
  `harness/tests/test_oc_app_*.cpp` (the frozen recipe in code).
- `plugins/apps/_per_app_runtime.h` (the runtime contract implementers build on but
  do not edit).

## 15. Preflight report (single checkpoint, after audit, before brainstorm)

Structured headers:

- Vendor pin verified (SHA matches).
- Per-candidate confirmation: subclasses `OC::App`, EASY holds against source, deps
  buildable, settings count, output type. If a candidate needs a shared subsystem
  on closer read, DEMOTE it (out-of-scope) and report; do not expand scope.
- GUID assignments (6 unique `OC??` codes, no clash with shipped `OCLR`/`OCHA` or
  the host/probe GUIDs).
- SegmentDisplay availability for SCALEEDITOR (shim provides it, or it becomes a
  bounded Layer 0 addition; if it needs a real subsystem, demote SCALEEDITOR).
- Any abort condition that fired.

## 16. Brainstorm requirements

Categorize the 6 against the frozen recipe, confirm EASY per source, assign GUIDs,
and for each state the output type (drives the DAC assertion) and the structural
test focus (zero-settings vs round-trip; cadence/edge modeling for the tick
accumulator). Name vendor `.cpp` deps per app. Status line per app. Upper-bound
abort: if more than 2 of 6 demote out of EASY, halt and reassess the batch
boundary.

## 17. Spec requirements

Canonical recipe (one reference walkthrough plus a recap/recipe section for adding
an instance) plus a one-paragraph entry per app (vendor source, GUID, output type,
settings, deps, test focus). Spec footer: Recipe spot-check, Per-entry verification
(trace 3 of the 6 apps end-to-end against vendor source), Shim prereq verification.

## 18. Plan requirements

DAG. Layer 0 (parent, sequential): only if the audit found a shared prerequisite
(e.g., a SegmentDisplay shim addition for SCALEEDITOR, or a `VENDOR_DEPS` build rule
gap), with baseline-green between commits. Layer 0.5 (parent): section markers in
`OC_APP_LIST` / shared Makefile lists if implementers would collide there;
integration owns the registration lines. Layer 1: parallel fan-out, one implementer
per app, each in a worktree branched from `dr/oc-pilot-apps` head after Layer 0.
Layer 2: integration on the feature branch (cherry-pick, resolve append-region
conflicts in `OC_APP_LIST` mechanically, run `make test-applets` + the OC suites +
`make arm`). Layer 3: hardware smoke (ADD each app via `mcp__nt_helper__new`;
spot-check render + one output per output-type class). Inline the worktree-dispatch
checklist and the pre-commit hook content. Declare parallelism explicitly.

## 19. Hard constraints

- Vendor sources are never edited.
- Every app ADDs on hardware, verified per app.
- serialise is addNumber-only; the customUI push-back adds `NT_parameterOffset()`.
- Full-scale modulation outputs stay within +-5V; pitch outputs are 1V/oct.
- All host tests green; each `.o` `.text` well under the ~82 KB cap.
- No edits to the integration-owned shared surface by implementers.

## 20. Lessons inherited (do not re-derive)

- The customUI push-back off-by-one: add `NT_parameterOffset()`; sim offset is 0, so
  tests use `nt::set_parameter_offset(1)`.
- DAC output is 16-bit code space, +-5V, 0V at code 32768; modulation apps write
  full-scale codes, never the pitch-only /1536 path.
- Screensaver is tick-based off `OC::CORE::ticks` (25 s); footer overlay suppressed
  by the draw-cache/step-restore pattern. Both are in the runtime already;
  implementers inherit them, do not re-add.
- Add-time hazards: serialise addNumber-only, `sr==0` guard. Both in the runtime.
- Per-`.o` isolation: each app has its own `HS::`/`OC::` globals and DAC store; no
  cross-app shared state.
- Deploy: `make deploy-sysex` hard-fails while nt_helper holds the port (free it
  first); a failed upload runs `newPreset()` and blanks the preset; reboot via sysex
  `0x7F`. nt_helper `edit_parameter` rejects numeric values (enum-only over MCP);
  batch `new([a,b])` intermittently drops the 2nd app (single `add()`/retry is
  reliable).
- Diagnostic discipline beats guessing on opaque link/relocation errors: enumerate
  with `objdump -r` / `nm`, do not strip-and-redeploy blindly.

## 21. Output paths

- Brainstorm: `docs/superpowers/brainstorms/2026-05-29-oc-pilot-apps-brainstorm.md`
  (use the run date).
- Spec: `docs/superpowers/specs/2026-05-29-oc-pilot-apps-design.md`.
- Plan: `docs/superpowers/plans/2026-05-29-oc-pilot-apps-plan.md`.

## 22. Abort budget (per layer, concrete thresholds)

- During audit (before preflight): vendor pin shifted; a candidate is actually
  HSApplication; more than 2 of 6 demote out of EASY (halt, reassess the boundary);
  SegmentDisplay turns out to need a real subsystem (demote SCALEEDITOR, proceed
  with 5).
- During planning: scope outside the 6; per-entry verification finds more than 1 of
  3 contradicting source AND a full re-audit finds more than 2 additional defects;
  plan exceeds a reasonable line budget for 6 apps.
- During parallel dispatch: more than 2 of 6 implementers abort substantively;
  any allowed-surface violation despite the hook; wallclock exceeds the slowest
  single port plus a 50 percent integration margin.
- During integration: `make test-applets` or the OC suites regress; `make arm`
  regresses; a `.text` exceeds the cap.
- During verification: more than 1 of 6 apps fails to ADD on hardware after the
  serialise/offset/DAC fixes are inherited (a failure here means a real defect, not
  the batch-add flakiness; distinguish by single-add retry).

## 23. Reporting (end-of-run)

Per app: GUID, worktree/branch/commit, host test result, `.text` size, hardware ADD
result, any vendor-reality adjustment. Plus: integration commit SHA, the OC-suite +
applet-suite + arm results, and any deferred finding. State the batch outcome
against the 6-app target (under-shipping with named blockers is success, not
failure).

## 24. Success criteria

All in-scope apps (6, or fewer if the audit demoted some with a named blocker) ship:
host suites green, ARM clean under cap, each ADDs and runs on hardware, outputs
correct for the app's output type. The recipe is shown to scale beyond the two
reference apps. Deferred items are tracked.

## 25. First actions

1. Audit (single bulleted block): verify vendor pin; read the primary references in
   full; per candidate confirm OC::App + EASY + deps + settings + output type +
   GUID; check SegmentDisplay availability; emit the preflight report; proceed
   unless an abort fired.
2. Layer 0 (if needed): land any shared prerequisite the audit surfaced, one commit
   at a time, baseline-green between.
3. Layer 0.5: section markers / registration scaffolding the implementers need.
4. Brainstorm, then spec (with the per-entry verification footer), then plan
   (DAG, hook, worktree-dispatch checklist).
5. Layer 1: dispatch the parallel implementers in one message, each in a worktree
   branched from `dr/oc-pilot-apps` head.
6. Layer 2: integrate, run the suites + arm.
7. Layer 3: hardware smoke (ADD each app, spot-check output per class), then open
   the PR.
