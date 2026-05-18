# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A compatibility shim that lets unmodified Phazerville Hemisphere applet sources compile and run as Expert Sleepers disting NT C++ plug-ins. Vendor source is pinned via git submodules and never edited. Everything project-specific lives in `shim/` and `applets/`.

## Bootstrap

```sh
./bootstrap.sh        # verifies host toolchain + arm-none-eabi-c++ + python deps
make vendor           # initializes the two pinned submodules
```

Required tools: `arm-none-eabi-c++` (for the NT target), `clang++` or `g++` (host), `python3`. `bootstrap.sh` contains OS-specific install hints for macOS and Debian/Ubuntu. Submodules: `vendor/distingNT_API` (Expert Sleepers SDK) and `vendor/O_C-Phazerville` (Hemisphere applet sources, currently pinned at SHA `7800d929`; verify with `git ls-tree HEAD vendor/O_C-Phazerville`).

## Build and test commands

| Command | Purpose |
| --- | --- |
| `make help` | List targets |
| `make arm` | Build all NT plug-ins under `build/arm/*.o` (the deployable artifact) |
| `make host` | Build host simulator binary |
| `make test-applets` | Build and run Catch2 host test binary for Hemisphere applet logic (the main test target during applet ports) |
| `make test-runtime` | Build and run NT runtime simulator tests |
| `make test-buses`, `test-draw`, `test-draw-shape`, `test-json`, `test-params`, `test-loader` | Per-subsystem host tests |
| `make test` | Run host build + applet tests + a scripted scenario |
| `make deploy DEVICE=/Volumes/NT` | Copy `build/arm/*.o` to a mounted NT in USB disk mode |
| `make deploy-sysex SYSEX_PLUGIN=build/arm/Hemispheres.o SYSEX_ID=0` | Push a built plug-in over USB-MIDI sysex (NT firmware v1.13+, no reboot) |
| `make clean` | Remove `build/` |

To run a single Catch2 test case, pass a tag to the binary directly: `./build/host/test_hemispheres '[cumulus]'` (build first via `make test-applets` or `make build/host/test_hemispheres`).

## Architecture

```
vendor/O_C-Phazerville/      vendor/distingNT_API/
   (Hemisphere applets,         (NT plug-in ABI:
    vendored unmodified)         _NT_algorithm,
            │                    _NT_parameter,
            ▼                    factory entry)
   shim/include/                       │
   (HemisphereApplet,                  │
    HSUtils, HSicons,                  ▼
    Phzicons, bus I/O)  ──►  applets/Hemispheres.cpp
                              (NT_HEMISPHERES_PLUGIN
                               pair-applet host)
                                       │
                                       ▼
                              harness/  or  device
                              (Catch2,      (deploy via
                               nt_runtime    USB-MIDI
                               simulator)    sysex or
                                             USB disk)
```

The project has three layers and one applet host.

**`vendor/` (read-only):** Submodules. `vendor/distingNT_API` provides the NT plug-in ABI (`_NT_algorithm`, `_NT_parameter`, factory entry points). `vendor/O_C-Phazerville/software/src/applets/*.h` provides Hemisphere applet sources, vendored unmodified. The shim's contract is that any applet header you find in `vendor/O_C-Phazerville/software/src/applets/` must compile against `shim/include/` without edits to the vendor source.

**`shim/include/` (the translation layer):** Headers that satisfy what Hemisphere applets reference (`HemisphereApplet` base class, `HSUtils.h` macros, `HSicons.h` shared icons, `PhzIcons.h` applet-specific icons, `HSClockManager.h`, `HSIOFrame.h`, `OC_core.h`, `OC_DAC.h`, `Arduino.h` stubs, the bus I/O API: `In`, `Out`, `Clock`, `Gate`, `ClockOut`, `GateOut`). Implementation in `shim/src/` (globals, graphics, icon bitmaps). `docs/shim-additions.md` is the per-applet ledger of what each port required beyond the previous baseline, audited so the shim surface stays minimized.

**`applets/Hemispheres.cpp` (the host):** A single NT plug-in (`NT_HEMISPHERES_PLUGIN`) that hosts two Hemisphere applets simultaneously (left and right side), with runtime selectors. Its parameter table maps gate inputs A-D, CV inputs A-D, and CV outputs A-D to the standard Phazerville Hemisphere bus layout. `shim/include/HemispheresFactory.h` holds the registration table (applet enum, name strings, `kMaxAppletSize`/`kMaxAppletAlign` `cmax` chains, `applet_factory()` table); `shim/include/applet_indices.h` is the slim enum-only header. To add an applet, the integration step touches both plus `shim/include/PhzIcons.h` and `shim/src/icons.cpp` for icon stubs.

**`harness/` (host test infrastructure):** `harness/src/nt_runtime.cpp` simulates the NT's audio frame loop in-process. `harness/src/plugin_loader.cpp` loads plug-ins through the same factory path as the device. `harness/tests/test_hemispheres.cpp` is the Catch2 binary used for applet behavior coverage; `harness/tests/applet_test_helpers.{h,cpp}` holds the `pack_<applet>` helpers that mirror each vendor `OnDataRequest` byte-by-byte. `harness/tests/test_buses.cpp`, `test_draw_text.cpp`, etc. cover non-applet subsystems. `harness/scripts/run_scenario.py` runs YAML-driven integration scenarios from `tests/scenarios/`.

## Critical gotcha: 10x clocked multiplier

The host harness runs the vendor `Controller()` 10 times per NT `step()` call (`ticks_this_step = numFrames / 3 = 10`). A single rising edge on `Clock(side)` sets `clocked[side] = true` once, but `clocked[side]` stays asserted across all 10 inner Controller calls in that buffer. Any applet whose Controller advances an internal counter, accumulator, or toggle inside `if (Clock(ch))` will fire 10 times per buffer per edge, not once.

Bus-level "fires once per N input edges" assertions are unreliable for these applets. Two valid coverage shapes:

1. Model the multiplier explicitly in the assertion math. See Cumulus CU2 at `harness/tests/test_hemispheres.cpp:1264` for the canonical commentary and Stairs ST3 / RunglBook RB3 for working examples.
2. Drop bus-level fire-count assertions; cover via round-trip plus state-injection only. See ProbabilityDivider PD3/PD4 for the template.

Per-applet entries in the design specs must state which shape they use. New applet ports MUST acknowledge this rule in their test concerns or test failures will look mysterious.

## Vendor compat headers compile against shim

Vendor non-applet headers (`HSProbLoopLinker.h`, `SegmentDisplay.h`, etc.) usually compile against the shim without needing a shim stub. They depend on `graphics` (already global in `hem_graphics.h`), `HEM_SIDE` (already in `HSUtils.h` via `using namespace HS;`), and `<cstdint>` types — all in scope by the time the include chain reaches them. Check whether the vendor header compiles as-is before preemptively writing a stub.

## Proportion is a free function (not a member)

`Proportion(numerator, denominator, max_value)` lives in `shim/include/util/util_math.h` as a free function mirroring vendor `util/util_math.h:48`. Vendor applets with nested helper structs (ADSREG's `MiniADSR`) call `Proportion(...)` unqualified; name lookup from inside a nested class binds to the enclosing class's inherited `HemisphereApplet::Proportion` as a non-static member and fails. Adding `Proportion` as a member of `HemisphereApplet` re-introduces this hazard. Keep it free.

## Changed() and changed_cv update

`HemisphereApplet::Changed(int ch)` reads `HS::frame.changed_cv[ch + channel_offset()]`. The shim populates `changed_cv[i]` in `step()` by comparing `inputs[i]` against a static `last_cv[4]`; a channel's flag flips when the delta exceeds `HEMISPHERE_CHANGE_THRESHOLD = 32` hem units (~1/8 semitone, vendor `HSUtils.h:25`).

## Single-shot gate tests must clear the bus between steps

`set_gate(bus, side, ch, 0, 8)` writes a 1-sample pulse at frame 0. Subsequent `step()` calls see the same bus and the shim's rising-edge detector refires every step unless `clear_bus(bus)` runs between. Tests that need a single `Clock` edge then a long step window MUST clear the bus after the edge-firing step, otherwise `StartADCLag` re-runs each buffer and `EndOfADCLag` never trips.

## IOFrame array-initializer quirk

`bool/int arr[4] = { -1 };` initializes ONLY `arr[0]` to -1; `arr[1..3]` default-initialize to 0. The shim's `adc_lag_countdown[4] = { -1 };` in `HSIOFrame.h` is the load-bearing example. Be careful when reading initial state of any per-channel array in the shim.

## Pack helper convention

`pack_<applet>` helpers in `harness/tests/applet_test_helpers.cpp` mirror the vendor `OnDataRequest` byte-by-byte. Rules:

- Use `int` for each field at the helper boundary; apply the vendor bias inside (e.g., `(value + 32)` for ClockDivider's `div[i] + 32`).
- AND with the field-width mask, not `0xFF`.
- Explicitly zero gap bits the vendor `OnDataRequest` skips (Cumulus zeroes bits 11..12; Voltage zeroes bit 9). Round-trip stability depends on this.
- Applets whose `OnDataRequest()` returns 0 (Button, GatedVCA, Switch) get no pack helper; their tests assert `OnDataRequest() == 0` directly.

## Phased work and document layout

Applet ports run in phases. Each phase produces three documents that are required to be auditable:

- `docs/superpowers/brainstorms/YYYY-MM-DD-<phase>-brainstorm.md` (scope, vendor SHA, categorization, status per applet)
- `docs/superpowers/specs/YYYY-MM-DD-<phase>-design.md` (canonical recipe + per-applet entries + spec footer with Recipe spot-check, Per-entry verification, Shim prereq verification)
- `docs/superpowers/plans/YYYY-MM-DD-<phase>-plan.md` (worklist; parallel by default; sequenced only where dependencies justify it)

Abort reports for failed phases live under `docs/superpowers/abort-reports/`. The Phase 3 retrospective (`2026-05-18-phase3-attempt-1-retrospective.md`) and the ResetClock spec-mismatch report (`2026-05-18-resetclock-spec-mismatch.md`) are required reading before any Phase 4+ kickoff because they encode the failure modes the framework now guards against.

Phase numbering convention: Phase 1+2 ported 14 applets (the existing baseline). Phase 3 retry ported 10 more (merged via squash commit `87596f4`; the underlying 12-commit history is preserved at `refs/archive/phase3-attempt-1/*` archive refs plus the brainstorm/spec/plan/retrospective documents). Phase 4 shipped 7 applets (squash `c68d6bc`, 4 Phase-3 deferrals plus 3 cat-B). Phase 5 pivoted at preflight from cat-C applet ports to a vendor dep port batch (kickoff at `docs/superpowers/prompts/2026-05-18-phase5-deps-kickoff.md`, merged `4df1c6a`): VectorOscillator+WaveformManager+RelabiManager bundled, Lorenz, tideslite+PhaseExtractor, full ClockManager, Quantizer subsystem (possibly split), CVInputMap, plus the time-injection helper. Phase 6 inherits the unblocked applet inventory and ports it under the standard parallel implementer pattern.

## Parallel execution

The project rule at `~/.claude/rules/parallel-execution.md` is load-bearing. Independent per-applet test ports are parallelized via isolated worktrees plus subagent dispatch in a single message. End-to-end wallclock equals the slowest single port plus integration, not the sum. Phase 3 attempt 1's failure traces directly to dispatching from `main` instead of the feature branch and to enforcing the implementer contract via prose instead of a pre-commit hook; both are now fixed and codified in the plan template. After `git worktree add`, run `git submodule update --init --recursive --depth=1` in the new worktree before any build; worktrees do not inherit submodule state.

A pre-commit hook at `.git/hooks/pre-commit` enforces the implementer contract: it rejects commits on `phase<N>-port/*` or `phase<N>-shim/*` branches that stage forbidden-surface shim files, and rejects commits on any branch not derived from the active feature branch. The hook is a no-op on other branches. Do not remove or weaken it without updating the active phase's plan; the framework relies on it.

Dep-port phases (Phase 5 shape) add a stricter hook rule: on `phase<N>-dep/*` branches the hook hard-rejects any commit that stages `shim/include/applet_indices.h`, `shim/include/HemispheresFactory.h`, or `shim/include/PhzIcons.h`. Those three files are owned by the integration step on the feature branch; dep implementers must never touch them. Operational enforcement of the "no applet ports in a dep-port phase" invariant.

## Workflow

Standard sequence for any non-trivial change:

1. **Brainstorm** the scope under `docs/superpowers/brainstorms/`. Vendor SHA at the top, status per applet, exclusions named.
2. **Spec** under `docs/superpowers/specs/` with the canonical recipe plus per-applet entries plus the spec footer (Recipe spot-check, Per-entry verification, Shim prereq verification).
3. **Plan** under `docs/superpowers/plans/` declaring parallelism, inlining the worktree-dispatch checklist and pre-commit hook content.
4. **Fan-out** via `superpowers:subagent-driven-development`. One implementer subagent per applet, each in its own worktree branched from the feature branch (never from `main`).
5. **Integration** on the feature branch: cherry-pick implementer commits, add registration entries, run `make test-applets` and `make arm`.
6. **PR** opened by the feature branch. Hardware smoke check happens after PR open since it needs physical access.

## Audit disciplines (Phase 5 lessons)

- Load-bearing infrastructure (helpers, shim subsystems) is designed in the kickoff prompt or preflight, not the brainstorm. Brainstorms categorize and select; they do not design under deadline.
- Cat-C demotion threshold: if more than 2 of N candidates carry deferred deps in audit, halt and assess whether the boundary is wrong, not just whether scope is small. High demotion rate is evidence of mis-drawn boundaries.
- Per-dep LoC counted in preflight drives split/monolithic decisions, not brainstorm wrestling. Thresholds: under 1500 monolithic, 1500-2250 split, over 2250 halt and carve sub-phase.
- Tiny deps (under ~150 LoC) that have no standalone use bundle into the bigger dep they serve. Two implementers for one logical unit is more risk than reward.

## Markdown discipline

After editing any `.md` file, run `markdownlint <file>` and fix all errors. The repo's `.markdownlint.json` relaxes a small set of rules (long lines, HTML, sibling-only duplicate headings); the rest are enforced.

## Deployment

`make deploy` requires the NT to be in USB disk mode (Misc menu) and mounted (default `/Volumes/NT`). `make deploy-sysex` requires NT firmware v1.13+ for the plug-in rescan sysex, and the NT must be free of any other USB-MIDI client. See `docs/hardware-deploy.md` and `docs/nt-sysex-protocol.md` for the full procedure.

## Merging PRs from a worktree

`gh pr merge` from inside a worktree fails when the default branch is checked out in the parent directory. Always pass `--repo danryan/nt_and_crime` and avoid `--delete-branch` (which triggers a local checkout). Use `gh pr merge <N> --squash --subject "..." --repo danryan/nt_and_crime`; delete the worktree and feature branch separately.

## Removing worktrees with submodules

`git worktree remove .worktrees/<name>` fails with "working trees containing submodules cannot be moved or removed". Always pass `--force` when removing a worktree that initialized submodules (every worktree in this repo does, since `make vendor` ran or submodules were init'd during provisioning).
