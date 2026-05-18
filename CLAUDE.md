# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A compatibility shim that lets unmodified Phazerville Hemisphere applet sources compile and run as Expert Sleepers disting NT C++ plug-ins. Vendor source is pinned via git submodules and never edited. Everything project-specific lives in `shim/` and `applets/`.

## Bootstrap

```sh
./bootstrap.sh        # verifies host toolchain + arm-none-eabi-c++ + python deps
make vendor           # initializes the two pinned submodules
```

Required tools: `arm-none-eabi-c++` (for the NT target), `clang++` or `g++` (host), `python3`. Submodules: `vendor/distingNT_API` (Expert Sleepers SDK) and `vendor/O_C-Phazerville` (Hemisphere applet sources, pinned at SHA `7800d929`).

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

The plan document inlines a worktree-dispatch checklist (parent agent verifies base branch, spec reachability, submodule init, pre-commit hook installation) and specifies a pre-commit hook content. Implementer subagents work in isolated worktrees branched from the feature branch (NEVER from `main`); the hook rejects commits on the wrong base and commits that stage forbidden-surface shim files.

Phase numbering convention: Phase 1+2 ported 14 applets (the existing baseline). Phase 3 retry ported 10 more. Phase 4 is bounded to 5 Phase-3 deferrals plus 5-8 category-B applets.

## Parallel execution

The project rule at `~/.claude/rules/parallel-execution.md` is load-bearing. Independent per-applet test ports are parallelized via isolated worktrees plus subagent dispatch in a single message. End-to-end wallclock equals the slowest single port plus integration, not the sum. Phase 3 attempt 1's failure traces directly to dispatching from `main` instead of the feature branch and to enforcing the implementer contract via prose instead of a pre-commit hook; both are now fixed and codified in the plan template.

## Markdown discipline

After editing any `.md` file, run `markdownlint <file>` and fix all errors. The repo's `.markdownlint.json` relaxes a small set of rules (long lines, HTML, sibling-only duplicate headings); the rest are enforced.

## Deployment

`make deploy` requires the NT to be in USB disk mode (Misc menu) and mounted (default `/Volumes/NT`). `make deploy-sysex` requires NT firmware v1.13+ for the plug-in rescan sysex, and the NT must be free of any other USB-MIDI client. See `docs/hardware-deploy.md` and `docs/nt-sysex-protocol.md` for the full procedure.
