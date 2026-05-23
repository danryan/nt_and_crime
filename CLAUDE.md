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

## Audit disciplines

- Load-bearing infrastructure (helpers, shim subsystems) is designed in the kickoff prompt or preflight, not the brainstorm. Brainstorms categorize and select; they do not design under deadline.
- Cat-C demotion threshold: if more than 2 of N candidates carry deferred deps in audit, halt and assess whether the boundary is wrong, not just whether scope is small. High demotion rate is evidence of mis-drawn boundaries.
- Per-dep LoC counted in preflight drives split/monolithic decisions, not brainstorm wrestling. Thresholds: under 1500 monolithic, 1500-2250 split, over 2250 halt and carve sub-phase.
- Tiny deps (under ~150 LoC) that have no standalone use bundle into the bigger dep they serve. Two implementers for one logical unit is more risk than reward.

## NT plug-in runtime

PIC plug-in builds require all-PIC linkage. The NT firmware applies relocations at on-device link time and treats `-fPIC` plug-ins as the expected shape. Mixing PIC plug-in code with non-PIC ARM asm (e.g., from `libgcc.a` extraction) produces "relocation of non-loaded section" warnings on every load. Stock `arm-none-eabi-gcc` ships no PIC `libgcc` multilib for `v7e-m+dp/hard` (verify with `arm-none-eabi-gcc -fPIC -print-libgcc-file-name` returning the same path as without `-fPIC`). Solution: vendor compiler-rt builtins under `shim/src/compiler_rt/` (verbatim from llvm-project tag `llvmorg-19.1.0`), compile each `.c` file with `arm-none-eabi-gcc` (NOT `c++` — C++ name-mangles EABI symbol names), and partial-link via `ld -r --strip-debug`. Apache-2.0 with LLVM exception licensing avoids GPL drag. `cxx_runtime_stubs.cpp` handles the C++ ABI symbols the runtime doesn't link: `__aeabi_atexit` (no-op), `__dso_handle` (nullptr), operator `new` (returns nullptr, arm-only), `std::__throw_bad_function_call` (spin, arm-only). NT firmware also doesn't provide certain vendor static class members: `SegmentDisplay::digit` is declared `static constexpr uint8_t digit[10]` but never defined out-of-class (C++11/14 odr-use bug); shim provides the out-of-class definition in `shim/src/globals.cpp`. `applets/aeabi_probe.cpp` is the permanent diagnostic for confirming which firmware symbols are unresolved after toolchain or vendor updates: deploy it via `make deploy-sysex SYSEX_PLUGIN=build/arm/aeabi_probe_stripped.o` and read the NT screen's first unresolved-symbol error.

Diagnostic discipline beats guessing. When stripping sections in sequence doesn't fix a "non-loaded section" error, the next move is `arm-none-eabi-objdump -r build/arm/Hemispheres.o | awk '$2 ~ /^R_ARM/ {print $2}' | sort | uniq -c` to enumerate actual relocation types, not another strip flag. The relocation table tells you what's wrong: `R_ARM_GOT32`/`R_ARM_GOTPC` reveal PIC code, `R_ARM_ABS32` reveals direct addressing. Mixed in a single `.o` is the failure pattern. Hardware deploys are slow; one minute reading `objdump -r` saves an hour of strip-and-redeploy cycles.

## NT firmware .text budget (~82KB per .o, scan-time)

NT firmware refuses to register a plug-in whose `.text` exceeds approximately 82KB. Misc > Plug-ins > View Info shows "Not enough memory for .text : <name>" and the entry as Failed. Empirically: 50KB loads, 81566 B loads, 83448 B fails. Section count is NOT the cap; the same firmware accepted a 12-section build at 81KB and refused a 14-section build at 83KB. Each `.o` is checked independently. If a single applet set exceeds the cap, ship it across multiple plug-ins via `HEMI_VARIANT`.

## Runtime ITC pool is shared across loaded slots

Two limits, not one:

- Scan-time cap (~82KB `.text` per `.o`): described above.
- Run-time pool (shared across all loaded algorithm instances): each slot loads its own copy of plug-in `.text` into ITC RAM. Total ITC across slots must fit a hardware budget (empirically ~100KB; one Hemispheres at ~65KB ITC alongside one Hemispheres2 at ~52KB ITC overflows). Error path is identical: "Not enough memory for .text : <name>" when adding the second algorithm to a preset.

NT API has no mechanism to share `.text` across plug-ins (`calculateStaticRequirements` shares DATA only). Hemispheres.o + Hemispheres2.o are alternates; users pick one set per preset.

## Diagnosing failed plug-in loads

Misc > Plug-ins > View Info shows pass/fail per `.o` with ITC/DTC/DRAM stats. Press a button on a Failed entry to see the detailed reason. Use `mcp__nt_helper__show_screen` to capture screen state in-session after `make deploy-sysex`; bisect plug-in size against the .text cap by iterating builds and screenshotting. `aeabi_probe.cpp` remains the diagnostic for unresolved-symbol errors.

## Loader does not honor custom code sections

NT firmware loader copies the canonical `.text` section verbatim into a fixed ITC buffer at registration. It does NOT recognize non-canonical executable section names (e.g., `.code_dram`, `.text.cold`). Functions tagged with `__attribute__((section(".code_dram")))` end up at unmapped addresses; the plug-in may register but adding it to a preset hard-faults the device when `step()` jumps into the unmapped region. Confirmed empirically with `applets/section_probe.cpp`: probe registered cleanly, hard-crashed on algorithm add. Power-cycle to recover; remove the bad `.o` via USB disk mode before next boot.

Practical consequence: there is no way to route cold methods to DRAM/OCRAM to dodge the per-`.o` `.text` cap. The cap is binding. Strategies that target it: aggressive code shrink (Q15 LUTs, kill `printf`, drop unused vendor deps) and `HEMI_VARIANT` splits across multiple `.o` files. The `section_probe.cpp` diagnostic stays in tree so future firmware updates can be re-verified.

## Construct-time parameterChanged hazard

Firmware fires `parameterChanged` for each parameter during the algorithm's construct path, before the algorithm is fully registered in the preset's slot table. Calling `NT_setParameterFromUi` back into self from inside that spurious `parameterChanged` hard-crashes the device on add-algorithm. Guards on `self->v != nullptr` and `NT_algorithmIndex(self) >= 0` are NOT sufficient; both return valid values during the construct-time fire. Confirmed via `plugins/probes/reentrancy_probe.cpp` (see `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md` Reentrancy result).

Mitigation pattern: a sentinel on the instance that flips true only after the algorithm is genuinely alive (e.g. `customUi` arming a flag just before forwarding, or `draw_count > 0` after `draw()` has run at least once). `parameterChanged` only forwards when the sentinel passes; construct-time spurious calls are dropped.

Separate firmware result from the same probe (load-bearing for proxy design): `NT_setParameterFromUi` does NOT re-enter `parameterChanged` synchronously (NEST counter stays at 0). Firmware defers the downstream notify to a later call frame. No stack-safety re-entry guard required.

## numParameters must match actual valid table range

Firmware reads `inst->parameters[i]` for `i` in `[0, numParameters)` and does not validate each entry. If `numParameters` is set to a buffer's max capacity but only `M` entries are populated, entries `[M, numParameters)` are zero-init `_NT_parameter{nullptr, 0, 0, 0, kNT_unitNone, 0, nullptr}` and become phantom parameters on the algo page (blank name, unsettable). For hosts with a dynamic parameter table sized to a worst-case Quadrants budget but K = 2 lanes used, the correct value is `K + K * kMaxProxyParamsPerSlot`, NOT `kMaxHostParams`. See `plugins/hosts/Hemispheres_host.cpp::calculateRequirements_impl` for the canonical formulation.

## Host-test seam: *_test_inject_slot

Three injection tables exist for host tests: `hh_test_inject_slot` (Hemispheres host), `qq_test_inject_slot` (Quadrants host), `host_proxy::hp_test_inject_slot` (shared aggregator). All gate on `NT_HEM_HOST_SIM` (set in `HOST_FLAGS`). Tests populate the table; the host's slot-resolution path reads from it instead of firmware `NT_getSlot`. Production code is unchanged. New host plug-in tests reuse the matching seam; do not invent a fourth.

## Catch2 main lives in harness/src/catch_main.cpp

Test files must not `#define CATCH_CONFIG_MAIN` or define `int main()`. The shared `harness/src/catch_main.cpp` (linked via `HARNESS_SRCS`) supplies Catch2's main entry point. Defining a second main causes a linker collision. Test files include `"catch.hpp"` and declare `TEST_CASE(...)` only.

## _NT_factory designated-initializer order

`_NT_factory` field order matters with C++20 designated initializers. The struct orders `tags` BEFORE `hasCustomUi`/`customUi` (`vendor/distingNT_API/include/distingnt/api.h:468`). Initializing `.tags = ...` after `.customUi = ...` is ill-formed and the compiler rejects it. The canonical `plugins/probes/aeabi_probe.cpp` factory doubles as the reference for field order when adding a new factory.

## ARM unresolved-symbol surface (firmware contract)

Firmware resolves at load: `NT_*` ABI (`NT_drawText`, `NT_screen`, `NT_jsonParse*`, `NT_intToString`), `_GLOBAL_OFFSET_TABLE_`, and newlib `memcpy`/`memset`/`memmove`/`strlen`/`strcmp`/`logf`/`powf`. NOT resolved: `__aeabi_d2lz` (handled by compiler_rt `fixdfdi.c` + `fixunsdfdi.c` + `fp_lib.h` + `int_math.h` vendored under `shim/src/compiler_rt/` from llvm-project tag `llvmorg-19.1.0`, Apache-2.0; `fixdfdi.c` emits the EABI alias `__aeabi_d2lz` via its internal `AEABI_RTABI` macro when `__ARM_EABI__` is defined), `snprintf` AND `vsnprintf` (newlib-nano omits both; inline integer format. `shim/src/graphics.cpp` and `shim/src/host_proxy.cpp::format_u2` are the precedents for two-digit unsigned formatting without pulling either). `arm-none-eabi-nm <plugin.o> | grep ' U '` enumerates.

## Vendor dep cpps must link into ARM plug-in

Vendor non-header-only `.cpp` files (not just headers) must compile into the ARM plug-in `.o`. Per-dep host tests cover them, but ARM-side calls only resolve if the cpps are linked into Hemispheres.{,2}.o. The current additions are `shim/src/lorenz/streams_{resources,lorenz_generator}.cpp`, listed in `VENDOR_DEP_ARM_OBJS` and pulled into the `BUILD_ARM_HEMI_VARIANT` rule. Symptom of missing link: `arm-none-eabi-nm` lists `_ZN7streams15LorenzGenerator4InitEh` or similar as unresolved.

## C++ COMDAT section merge in ARM partial-link

gcc emits one `.text._ZN<mangled>` COMDAT section per inline class method. `ld -r` preserves them under SHF_GROUP even with a `-T SECTIONS{}` script. Pipeline collapses them: `arm-none-eabi-objcopy --remove-section='.group'` followed by `ld -r -T shim/merge_sections.lds` merges all `.text._Z*`/`.data.rel.ro._Z*`/etc into single canonical sections. Drops ~1640 sections to ~14 with no behavior change. The section explosion is NOT a firmware cap (verified by deploying a 12-section 83KB build that still failed), but the cleaner artifact is easier to inspect and ship. Use `arm-none-eabi-readelf -W -S` (not `objdump -h`, which truncates) to inspect full mangled section names.

## Factory variants: HEMI_VARIANT

`shim/include/HemispheresFactory.h` gates the on-device applet set by `HEMI_VARIANT` set per `.o` via Makefile:

- `0` = host build (default; all 56 applets, tests use this)
- `1` = ARM `Hemispheres.o` primary: 51 applets (~82KB text)
- `2` = ARM `Hemispheres2.o` secondary: 5 largest applets (Relabi, Shredder, EnsOscKey, VectorLFO, Strum)

Applets dropped from a variant get `make_applet<Empty>` stubs in `factory_table` and skipped `#include`. `kMaxAppletSize`/`kMaxAppletAlign` hardcoded to 1024/16 in variants 1-2 (cmax chain needs all types in scope, only available in variant 0). New applets choose variant by per-class `.text` size measured on the pre-merge `Hemispheres.raw.o` (`arm-none-eabi-readelf -W -S`); greedy-keep favors smallest text in primary to maximize on-device applet count.

## Plugin directory layout (per-applet pilot release onward)

`plugins/applets/`, `plugins/hosts/`, `plugins/probes/` are the canonical
locations for new per-applet plug-ins, host plug-ins, and diagnostic
probes. The top-level `applets/` directory is deprecated; its remaining
contents (`Hemispheres.cpp`, `Hemispheres2.cpp`) will move or be removed
in the cleanup release after the mass-port release ships. Probes
(`aeabi_probe`, `bus_probe`, `section_probe`, `solo_probe`) moved to
`plugins/probes/` during the pilot release setup commits.

Per-applet plug-ins compile via the `BUILD_PER_APPLET` Makefile macro
and the `PILOT_APPLET_LIST` variable. Each per-applet `.o` is small
(16-20 KB `.text`), well under the firmware's ~82 KB per-`.o` cap.
Hosts (`Hemispheres_host.o`, `Quadrants_host.o`) compose per-applet
plug-ins at runtime through the firmware `_NT_slot` API plus the
versioned `HemiPluginInterface` function-pointer-in-data ABI.

## Markdown discipline

After editing any `.md` file, run `markdownlint <file>` and fix all errors. The repo's `.markdownlint.json` relaxes a small set of rules (long lines, HTML, sibling-only duplicate headings); the rest are enforced.

## Deployment

`make deploy` requires the NT to be in USB disk mode (Misc menu) and mounted (default `/Volumes/NT`). `make deploy-sysex` requires NT firmware v1.13+ for the plug-in rescan sysex, and the NT must be free of any other USB-MIDI client. See `docs/hardware-deploy.md` and `docs/nt-sysex-protocol.md` for the full procedure.

## Merging PRs from a worktree

`gh pr merge` from inside a worktree fails when the default branch is checked out in the parent directory. Always pass `--repo danryan/nt_and_crime` and avoid `--delete-branch` (which triggers a local checkout). Use `gh pr merge <N> --squash --subject "..." --repo danryan/nt_and_crime`; delete the worktree and feature branch separately.

## Removing worktrees with submodules

`git worktree remove .worktrees/<name>` fails with "working trees containing submodules cannot be moved or removed". Always pass `--force` when removing a worktree that initialized submodules (every worktree in this repo does, since `make vendor` ran or submodules were init'd during provisioning).

## Hardware deploy: SRAM-size cache invalidation

Firmware caches `calculateRequirements` at plug-in scan time. Enlarging any `_NT_algorithm` subclass (adding a member, growing a buffer) requires a power cycle on the NT before the new build behaves correctly. Without it, the runtime allocates the prior smaller size, `construct()` overruns into adjacent memory, and downstream reads return garbage that often masquerades as expected behavior (e.g. a `step()` memcpy "restore" reading pixels indistinguishable from the firmware overlay it was supposed to overwrite). Power cycle re-reads the requirement; the build then works.

## Firmware footer overdraw pattern (Hemispheres + Quadrants hosts)

NT firmware paints its helper-text overlay onto `NT_screen` after the algorithm's `draw()` returns. `step()` runs at audio rate AFTER the firmware overlay pass and BEFORE the next display flush; writes to `NT_screen` from `step()` reach the display. Both composer hosts use this to suppress the overlay: `draw()` snapshots the bottom 16 rows into a per-instance `footer_cache[16*128]`; `step()` restores them while `steps_since_draw < kFooterRestoreSteps` (navigate-away guard, ~67 ms at 48 kHz/32-frame buffers). The counter is reset in `draw()` and incremented in `step()`. See `plugins/hosts/Hemispheres_host.cpp` `step_impl` + `draw_impl` tail. No `hasCustomUi` changes needed; softkey nav stays intact.

## HS:: globals are per-TU, not shared

Each per-applet ARM `.o` privately aggregates `shim/src/globals.cpp` via the `_per_applet_runtime.h` -> `hem_shim.h` -> `hem_shim_impl.h` include chain. Every plug-in therefore has its own copy of every `HS::*` global (`gfx_offset`, `gfx_clip_w`, `clock_m`, `popup_type`, ...). Writes from one TU do NOT reach reads from another TU. Implication: host plug-ins cannot drive per-applet rendering by writing `HS::*` from the host's TU; the write must happen in the same TU that reads it (e.g. `_per_applet_runtime.h::render_view_with_offset`). Hosts have no `globals.o` linked and `arm-none-eabi-nm` shows `HS::*` symbols as unresolved if the host references them.

## Per-applet standalone customUi mapping

Standalone per-applet plug-ins (loaded without a Hemispheres/Quadrants host) route firmware control events through `plugins/applets/_per_applet_runtime.h::route_custom_ui`. Current mapping: `kNT_encoderL` -> `on_encoder_turn` and `kNT_encoderButtonL` -> `on_button_press`. `kNT_button1` is intentionally NOT claimed in standalone; the applet's `on_aux_button` switches many vendor applets into param-edit mode and routing it to button 1 in standalone view ended up changing parameters unexpectedly. Hardware button 1 is therefore handled by the firmware default. Host plug-ins have their own routing in `plugins/hosts/*_host.cpp::customUi_impl` and forward button events to per-applet `on_aux_button` from there.

## Makefile prerequisite expansion timing

`$(VAR)` references in prerequisite lists expand at rule-parse time, not at rule-evaluation time. A `:=` variable defined AFTER its use in a prereq expands to empty. `SHIM_CORE_SRCS` was previously defined after its uses in `test_host_%`, `test_applet_%`, and `test_dep_%` rules; the empty expansion silently worked while no host TU referenced anything from the listed sources. Adding any new host reference to symbols defined in `shim/src/globals.cpp` / `shim/src/graphics.cpp` / `shim/src/icons.cpp` / `shim/src/quant/*` / `shim/src/cv_map/*` will surface the missing link. Fix: keep variable definitions ABOVE the first rule that uses them in a prereq.
