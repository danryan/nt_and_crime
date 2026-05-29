# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A compatibility shim that lets unmodified Phazerville Hemisphere applet sources compile and run as Expert Sleepers disting NT C++ plug-ins. Vendor source is pinned via git submodules and never edited. Everything project-specific lives in `shim/` and `plugins/`. The top-level `applets/` directory and the bundled-host build were removed in the 2026-05-23 cleanup release; per-applet plug-ins under `plugins/applets/` plus composer hosts under `plugins/hosts/` are the only on-device shape.

## Bootstrap

```sh
./bootstrap.sh        # verifies toolchain + python deps, provisions all submodules
make vendor           # runs ./bootstrap.sh (canonical submodule provisioning entry)
```

Required tools: `arm-none-eabi-c++` (for the NT target), `clang++` or `g++` (host), `python3`. `bootstrap.sh` contains OS-specific install hints for macOS and Debian/Ubuntu. Three submodules: `vendor/distingNT_API` (Expert Sleepers SDK), `vendor/O_C-Phazerville` (Hemisphere applet sources, pinned at SHA `7800d929`; verify with `git ls-tree HEAD vendor/O_C-Phazerville`), and `vendor/llvm-project` (compiler-rt builtins, pinned at tag `llvmorg-19.1.0`, sparse-checkout limited to `compiler-rt/lib/builtins/`). `make vendor` delegates to `bootstrap.sh` because the sparse-checkout config for `vendor/llvm-project` is NOT carried by a plain `git submodule update`; the bootstrap block clones it sparsely and re-applies sparse config on every run. See "Adding a sparse-checkout vendor submodule" below.

## Build and test commands

| Command | Purpose |
| --- | --- |
| `make help` | List targets |
| `make arm` | Build all NT plug-ins under `build/arm/*.o` (the deployable artifact) |
| `make host` | Build host simulator binary |
| `make test-applets` | Run per-applet Catch2 host test binaries (alias for `test-applets-pilot`; the main test target during applet ports) |
| `make test-runtime` | Build and run NT runtime simulator tests |
| `make test-buses`, `test-draw`, `test-draw-shape`, `test-json`, `test-params`, `test-loader` | Per-subsystem host tests |
| `make test` | Run host build + applet tests + a scripted scenario |
| `make deploy DEVICE=/Volumes/NT` | Copy `build/arm/*.o` to a mounted NT in USB disk mode |
| `make deploy-sysex SYSEX_PLUGIN=build/arm/Hemispheres_host.o SYSEX_ID=0` | Push a built plug-in over USB-MIDI sysex (NT firmware v1.13+, no reboot) |
| `make clean` | Remove `build/` |

To run a single Catch2 test case, pass a tag to the matching per-applet binary directly: `./build/host/test_applet_Cumulus '[cumulus]'` (build first via `make build/host/test_applet_Cumulus`).

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
    Phzicons, bus I/O)  ──►  plugins/applets/<APPLET>.cpp
                              (one per-applet .o each)
                                       │
                              plugins/hosts/*_host.cpp
                              (composer hosts load
                               per-applet plug-ins at
                               runtime via HemiPluginInterface)
                                       │
                                       ▼
                              harness/  or  device
                              (Catch2,      (deploy via
                               nt_runtime    USB-MIDI
                               simulator)    sysex or
                                             USB disk)
```

The project has three layers (vendor, shim, plugins) plus the harness.

**`vendor/` (read-only):** Submodules. `vendor/distingNT_API` provides the NT plug-in ABI (`_NT_algorithm`, `_NT_parameter`, factory entry points). `vendor/O_C-Phazerville/software/src/applets/*.h` provides Hemisphere applet sources, vendored unmodified. The shim's contract is that any applet header you find in `vendor/O_C-Phazerville/software/src/applets/` must compile against `shim/include/` without edits to the vendor source.

**`shim/include/` (the translation layer):** Headers that satisfy what Hemisphere applets reference (`HemisphereApplet` base class, `HSUtils.h` macros, `HSicons.h` shared icons, `PhzIcons.h` applet-specific icons, `HSClockManager.h`, `HSIOFrame.h`, `OC_core.h`, `OC_DAC.h`, `Arduino.h` stubs, the bus I/O API: `In`, `Out`, `Clock`, `Gate`, `ClockOut`, `GateOut`). Implementation in `shim/src/` (globals, graphics, icon bitmaps). `docs/shim-additions.md` is the per-applet ledger of what each port required beyond the previous baseline, audited so the shim surface stays minimized.

**`plugins/` (the deployable units):** Each `plugins/applets/<APPLET>.cpp` is one NT plug-in compiling to one small `.o` (16-20 KB `.text`); it includes its manifest at `shim/include/applet_manifests/<APPLET>.h`, the per-applet runtime header `plugins/applets/_per_applet_runtime.h`, and the vendor applet header directly. `plugins/hosts/Hemispheres_host.cpp` (GUID `HmHh`, 2 lanes) and `plugins/hosts/Quadrants_host.cpp` (GUID `QdHh`, 4 lanes) are composer hosts that load per-applet plug-ins at runtime through the firmware `_NT_slot` API plus the versioned `HemiPluginInterface` function-pointer-in-data ABI. To add an applet, create its `plugins/applets/<APPLET>.cpp` plus `shim/include/applet_manifests/<APPLET>.h`, add it to `ALL_APPLET_LIST` in the Makefile, and add icon stubs in `shim/include/PhzIcons.h` plus `shim/src/icons.cpp` if needed.

**`harness/` (host test infrastructure):** `harness/src/nt_runtime.cpp` simulates the NT's audio frame loop in-process. `harness/src/plugin_loader.cpp` loads plug-ins through the same factory path as the device. `harness/tests/test_applet_<APPLET>.cpp` is the per-applet Catch2 binary used for applet behavior coverage; each file carries its own local `pack_<applet>` helper that mirrors the vendor `OnDataRequest` byte-by-byte (see "Pack helper convention" below). `harness/tests/test_buses.cpp`, `test_draw_text.cpp`, etc. cover non-applet subsystems. `harness/scripts/run_scenario.py` runs YAML-driven integration scenarios from `tests/scenarios/`.

## Critical gotcha: 10x clocked multiplier

The host harness runs the vendor `Controller()` 10 times per NT `step()` call (`ticks_this_step = numFrames / 3 = 10`). A single rising edge on `Clock(side)` sets `clocked[side] = true` once, but `clocked[side]` stays asserted across all 10 inner Controller calls in that buffer. Any applet whose Controller advances an internal counter, accumulator, or toggle inside `if (Clock(ch))` will fire 10 times per buffer per edge, not once.

Bus-level "fires once per N input edges" assertions are unreliable for these applets. Two valid coverage shapes:

1. Model the multiplier explicitly in the assertion math. See Cumulus per-applet test `harness/tests/test_applet_Cumulus.cpp` for the canonical commentary and Stairs ST3 / RunglBook RB3 for working examples.
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

`pack_<applet>` helpers in each `harness/tests/test_applet_<APPLET>.cpp` mirror the vendor `OnDataRequest` byte-by-byte. Each per-applet test owns its own local copy of the relevant helper. Rules:

- Use `int` for each field at the helper boundary; apply the vendor bias inside (e.g., `(value + 32)` for ClockDivider's `div[i] + 32`).
- AND with the field-width mask, not `0xFF`.
- Explicitly zero gap bits the vendor `OnDataRequest` skips (Cumulus zeroes bits 11..12; Voltage zeroes bit 9). Round-trip stability depends on this.
- Applets whose `OnDataRequest()` returns 0 (Button, GatedVCA, Switch) get no pack helper; their tests assert `OnDataRequest() == 0` directly.

## Document layout

Non-trivial changes produce three auditable documents:

- `docs/superpowers/brainstorms/YYYY-MM-DD-<topic>-brainstorm.md` (scope, vendor SHA, categorization, status)
- `docs/superpowers/specs/YYYY-MM-DD-<topic>-design.md` (canonical recipe + per-item entries + spec footer with Recipe spot-check, Per-entry verification, Shim prereq verification)
- `docs/superpowers/plans/YYYY-MM-DD-<topic>-plan.md` (worklist; parallel by default; sequenced only where dependencies justify it)

Retired topic docs are consolidated under `docs/superpowers/archived/<topic>/` with a per-topic `INDEX.md`. Abort reports live under `docs/superpowers/abort-reports/`. The Phase 3 attempt-1 retrospective (`2026-05-18-phase3-attempt-1-retrospective.md`) and the ResetClock spec-mismatch report (`2026-05-18-resetclock-spec-mismatch.md`) are still required reading before any parallel subagent fan-out: they encode the failure modes (dispatching from `main` instead of the feature branch; enforcing the implementer contract via prose instead of a hook) that the worktree-dispatch discipline now guards against.

## Parallel execution

The project rule at `~/.claude/rules/parallel-execution.md` is load-bearing. Independent units are parallelized via isolated worktrees plus subagent dispatch in a single message. End-to-end wallclock equals the slowest single unit plus integration, not the sum. Dispatch each implementer in a worktree branched from the feature branch, never from `main`. After `git worktree add` (or `EnterWorktree`), run `git submodule update --init --recursive --depth=1` in the new worktree before any build; worktrees do not inherit submodule state.

## Workflow

Standard sequence for any non-trivial change:

1. **Brainstorm** the scope under `docs/superpowers/brainstorms/`. Vendor SHA at the top, status per item, exclusions named.
2. **Spec** under `docs/superpowers/specs/` with the canonical recipe plus per-item entries plus the spec footer (Recipe spot-check, Per-entry verification, Shim prereq verification).
3. **Plan** under `docs/superpowers/plans/` declaring parallelism, inlining the worktree-dispatch checklist.
4. **Fan-out** via `superpowers:subagent-driven-development`. One implementer subagent per unit, each in its own worktree branched from the feature branch (never from `main`).
5. **Integration** on the feature branch: cherry-pick implementer commits, run `make test-applets` and `make arm`.
6. **PR** opened by the feature branch. Hardware smoke check happens after PR open since it needs physical access.

## Audit disciplines

- Load-bearing infrastructure (helpers, shim subsystems) is designed in the spec, not improvised under deadline in the brainstorm. Brainstorms categorize and select; they do not design.
- When auditing a worklist, a high rate of items needing deferral is evidence the boundary is mis-drawn, not just that scope is small. Halt and reassess the boundary rather than shrinking item-by-item.

## NT plug-in runtime

PIC plug-in builds require all-PIC linkage. The NT firmware applies relocations at on-device link time and treats `-fPIC` plug-ins as the expected shape. Mixing PIC plug-in code with non-PIC ARM asm (e.g., from `libgcc.a` extraction) produces "relocation of non-loaded section" warnings on every load. Stock `arm-none-eabi-gcc` ships no PIC `libgcc` multilib for `v7e-m+dp/hard` (verify with `arm-none-eabi-gcc -fPIC -print-libgcc-file-name` returning the same path as without `-fPIC`). Solution: source compiler-rt builtins from `vendor/llvm-project/compiler-rt/lib/builtins/` (submodule pinned at `llvmorg-19.1.0`, sparse-checkout limited to that tree), compile each `.c` file with `arm-none-eabi-gcc` (NOT `c++` — C++ name-mangles EABI symbol names), and partial-link via `ld -r --strip-debug`. Apache-2.0 with LLVM exception licensing avoids GPL drag. `cxx_runtime_stubs.cpp` handles the C++ ABI symbols the runtime doesn't link: `__aeabi_atexit` (no-op), `__dso_handle` (nullptr), operator `new` (returns nullptr, arm-only), `std::__throw_bad_function_call` (spin, arm-only). NT firmware also doesn't provide certain vendor static class members: `SegmentDisplay::digit` is declared `static constexpr uint8_t digit[10]` but never defined out-of-class (C++11/14 odr-use bug); shim provides the out-of-class definition in `shim/src/globals.cpp`. `plugins/probes/aeabi_probe.cpp` is the permanent diagnostic for confirming which firmware symbols are unresolved after toolchain or vendor updates: deploy it via `make deploy-sysex SYSEX_PLUGIN=build/arm/aeabi_probe.o` and read the NT screen's first unresolved-symbol error.

Diagnostic discipline beats guessing. When stripping sections in sequence doesn't fix a "non-loaded section" error, the next move is `arm-none-eabi-objdump -r build/arm/Hemispheres_host.o | awk '$2 ~ /^R_ARM/ {print $2}' | sort | uniq -c` to enumerate actual relocation types, not another strip flag. The relocation table tells you what's wrong: `R_ARM_GOT32`/`R_ARM_GOTPC` reveal PIC code, `R_ARM_ABS32` reveals direct addressing. Mixed in a single `.o` is the failure pattern. Hardware deploys are slow; one minute reading `objdump -r` saves an hour of strip-and-redeploy cycles.

## NT firmware .text budget (~82KB per .o, scan-time)

NT firmware refuses to register a plug-in whose `.text` exceeds approximately 82KB. Misc > Plug-ins > View Info shows "Not enough memory for .text : <name>" and the entry as Failed. Empirically: 50KB loads, 81566 B loads, 83448 B fails. Section count is NOT the cap; the same firmware accepted a 12-section build at 81KB and refused a 14-section build at 83KB. Each `.o` is checked independently. The per-applet model keeps every plug-in small (16-20 KB `.text`), so the cap is no longer pressing for the shipped set; it matters when adding a heavy single applet or growing a composer host.

## Runtime ITC pool is shared across loaded slots

Two limits, not one:

- Scan-time cap (~82KB `.text` per `.o`): described above.
- Run-time pool (shared across all loaded algorithm instances): each slot loads its own copy of plug-in `.text` into ITC RAM. Total ITC across slots must fit a hardware budget (empirically ~100KB). With the per-applet model each loaded applet contributes only its own 16-20 KB, so many applets coexist; the limit is reached only by loading many slots at once. Error path is identical: "Not enough memory for .text : <name>" when adding an algorithm to a preset.

NT API has no mechanism to share `.text` across plug-ins (`calculateStaticRequirements` shares DATA only).

## Diagnosing failed plug-in loads

Misc > Plug-ins > View Info shows pass/fail per `.o` with ITC/DTC/DRAM stats. It does NOT expand a per-plugin failure reason on a Failed entry (the screen stays at the summary); bisect plug-in size against the `.text` cap by iterating builds and screenshotting instead. Use `mcp__nt_helper__show_screen` to capture screen state in-session after `make deploy-sysex`. `aeabi_probe.cpp` remains the diagnostic for unresolved-symbol errors.

nt_helper MCP has no plug-in-file-upload tool (preset/param/slot/routing/screen only); upload `.o` files via `make deploy-sysex` or the nt_helper app. Its `edit_parameter` rejects numeric values ("requires a numeric value") because the untyped value field is stringified in transit; only enum-string edits work over MCP. Batch `new([a,b])` intermittently drops the second algorithm ("did not appear"); a single `add()` or a retry is reliable.

To confirm a plug-in REGISTERED (vs silently rejected), enumerate over sysex rather than reading the screen: command `0x30` returns the algorithm count, `0x31 <index>` returns name, 4-byte GUID, and an `isPlugin` byte (see the upstream `thorinside/nt_helper` `docs/SYSEX_REFERENCE.md`, distilled locally in `docs/nt-sysex-protocol.md`). Match your GUID (e.g. `HmHh`, `QdHh`) in the enumeration. There is NO sysex opcode that returns a scan-failure reason; a rejected plug-in simply does not appear in the `0x31` list.

## Loader does not honor custom code sections

NT firmware loader copies the canonical `.text` section verbatim into a fixed ITC buffer at registration. It does NOT recognize non-canonical executable section names (e.g., `.code_dram`, `.text.cold`). Functions tagged with `__attribute__((section(".code_dram")))` end up at unmapped addresses; the plug-in may register but adding it to a preset hard-faults the device when `step()` jumps into the unmapped region. Confirmed empirically with `plugins/probes/section_probe.cpp`: probe registered cleanly, hard-crashed on algorithm add. Power-cycle to recover; remove the bad `.o` via USB disk mode before next boot.

Practical consequence: there is no way to route cold methods to DRAM/OCRAM to dodge the per-`.o` `.text` cap. The cap is binding. Strategies that target it: aggressive code shrink (Q15 LUTs, kill `printf`, drop unused vendor deps) and, for a single applet that overflows, splitting its work. The per-applet model already keeps each `.o` small. The `section_probe.cpp` diagnostic stays in tree so future firmware updates can be re-verified.

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

Firmware resolves at load: `NT_*` ABI (`NT_drawText`, `NT_screen`, `NT_jsonParse*`, `NT_intToString`), `_GLOBAL_OFFSET_TABLE_`, and newlib `memcpy`/`memset`/`memmove`/`strlen`/`strcmp`/`logf`/`powf`. NOT resolved: `__aeabi_d2lz` (handled by compiler_rt `fixdfdi.c` + `fixunsdfdi.c` + `fp_lib.h` + `int_math.h` from `vendor/llvm-project/compiler-rt/lib/builtins/` (submodule pinned at `llvmorg-19.1.0`), Apache-2.0; `fixdfdi.c` emits the EABI alias `__aeabi_d2lz` via its internal `AEABI_RTABI` macro when `__ARM_EABI__` is defined), `snprintf` AND `vsnprintf` (newlib-nano omits both; inline integer format. `shim/src/graphics.cpp` and `shim/src/host_proxy.cpp::format_u2` are the precedents for two-digit unsigned formatting without pulling either). `arm-none-eabi-nm <plugin.o> | grep ' U '` enumerates.

## Vendor dep cpps must link into ARM plug-in

Vendor non-header-only `.cpp` files (not just headers) must compile into the ARM plug-in `.o`. Per-dep host tests cover them, but ARM-side calls only resolve if the cpps are linked into the consuming applet's `.o`. Linkage is per-applet via `VENDOR_DEPS_<APPLET>` in the Makefile: e.g. `VENDOR_DEPS_LowerRenz` lists `build/arm/vendor_src/streams_{resources,lorenz_generator}.o`, compiled in place from `$(HEM_SRC_DIR)` by the `build/arm/vendor_src/%.o` rule and pulled into the `BUILD_PER_APPLET` rule for LowerRenz only. Applets that need no vendor cpp leave their `VENDOR_DEPS_*` empty. Symptom of missing link: `arm-none-eabi-nm` lists `_ZN7streams15LorenzGenerator4InitEh` or similar as unresolved.

## C++ COMDAT section merge in ARM partial-link

gcc emits one `.text._ZN<mangled>` COMDAT section per inline class method. `ld -r` preserves them under SHF_GROUP even with a `-T SECTIONS{}` script. Pipeline collapses them: `arm-none-eabi-objcopy --remove-section='.group'` followed by `ld -r -T shim/merge_sections.lds` merges all `.text._Z*`/`.data.rel.ro._Z*`/etc into single canonical sections. Drops ~1640 sections to ~14 with no behavior change. The section explosion is NOT a firmware cap (verified by deploying a 12-section 83KB build that still failed), but the cleaner artifact is easier to inspect and ship. Use `arm-none-eabi-readelf -W -S` (not `objdump -h`, which truncates) to inspect full mangled section names.

## Plugin directory layout (per-applet pilot release onward)

`plugins/applets/`, `plugins/hosts/`, `plugins/probes/` are the canonical
locations for per-applet plug-ins, host plug-ins, and diagnostic
probes. The top-level `applets/` directory was removed in the 2026-05-23
cleanup release. Probes (`aeabi_probe`, `bus_probe`, `section_probe`,
`solo_probe`, `reentrancy_probe`) live under `plugins/probes/`.

Per-applet plug-ins compile via the `BUILD_PER_APPLET` Makefile macro
and the `ALL_APPLET_LIST` variable (`PILOT_APPLET_LIST` is a
backwards-compat alias). Each per-applet `.o` is small (16-20 KB
`.text`), well under the firmware's ~82 KB per-`.o` cap. Hosts
(`Hemispheres_host.o`, `Quadrants_host.o`) compose per-applet plug-ins
at runtime through the firmware `_NT_slot` API plus the versioned
`HemiPluginInterface` function-pointer-in-data ABI.

## Adding a sparse-checkout vendor submodule

`vendor/llvm-project` (compiler-rt builtins) is added as a sparse
submodule. `.gitignore` ignores `vendor/*`, and a plain
`git submodule update` does NOT carry sparse-checkout config, so two
non-obvious steps are required (both codified in `bootstrap.sh`):

- `git add -f vendor/<name>` to stage the gitlink past the `vendor/*`
  ignore rule (the existing submodules were added before that rule).
- `git submodule absorbgitdirs vendor/<name>` only works AFTER the path
  is staged; run it after `git add -f`, not before.

`bootstrap.sh` carries an idempotent clone-vs-reapply block: on a fresh
checkout it clones `--no-checkout --depth=1 --filter=blob:none`, applies
`sparse-checkout set compiler-rt/lib/builtins`, checks out the tag, and
absorbs the git dir; on re-runs (including new worktrees) it runs
`sparse-checkout reapply`. `make vendor` calls `bootstrap.sh` so every
provisioning path applies the sparse config. Verify with
`git -C vendor/llvm-project sparse-checkout list` (and
`test ! -d vendor/llvm-project/llvm` confirms the full tree was not
pulled; the sparse tree is ~2 MB, not multi-GB).

## Markdown discipline

After editing any `.md` file, run `markdownlint <file>` and fix all errors. The repo's `.markdownlint.json` relaxes a small set of rules (long lines, HTML, sibling-only duplicate headings); the rest are enforced.

## Deployment

`make deploy` requires the NT to be in USB disk mode (Misc menu) and mounted (default `/Volumes/NT`). `make deploy-sysex` requires NT firmware v1.13+ for the plug-in rescan sysex, and the NT must be free of any other USB-MIDI client. See `docs/hardware-deploy.md` and `docs/nt-sysex-protocol.md` for the full procedure.

`make deploy-sysex` (`push_plugin_to_device.py`) needs exclusive USB-MIDI. While the nt_helper MCP client holds the port the upload hard-fails ("Error uploading plug-in file!") on every attempt, not intermittently; free the port (disconnect nt_helper), upload, then reconnect. The script runs `newPreset()` before uploading, so a failed upload blanks the device preset. A sysex `0x7F` reboot (send via `mido`) reloads plug-in code and re-caches the SRAM requirement; the device auto-loads its last preset on boot.

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

## O_C full-screen apps foundation

Beyond the Hemisphere applets, the shim supports porting vendor O_C full-screen apps (the `OC::App` struct: `isr`/`loop`/`DrawMenu`/`DrawScreensaver`/`HandleButtonEvent`/`HandleEncoderEvent`/`HandleAppEvent`/`Save`/`Restore`) as independent NT plug-ins, one app per `.o` under `plugins/apps/`. The foundation landed on `dr/oc-apps-foundation` validated against Low-rents (`plugins/apps/Low_rents.cpp`) and Harrington 1200 (`plugins/apps/Harrington1200.cpp`); both `.text` are 11-15 KB, far under the cap. The design and the deviations discovered during the build are in `docs/superpowers/specs/2026-05-27-oc-apps-foundation-design.md` (see its "Implementation notes" section).

Shim surface: `shim/include/OC_apps.h` (App struct, `OC::apps` namespace), `OC_ui.h` (`UiControl` enum), `OC_ADC.h`/`OC_digital_inputs.h` (plus `DIGITAL_INPUT_*_MASK`)/extended `OC_DAC.h` (channel objects, output history, voltage scaling), `OC_menus.h` (hand-ported `menu::` widgets on `shim::Graphics`, plus `vectorscope_render`/`visualize_pitch_classes`), `OC_config.h`, extended `OC_strings.h`/`hem_graphics.h`. Vendor `OC_menus.h` cannot compile (it hard-includes the Teensy display driver), so the menu layer is hand-ported, not vendor-compiled.

Per-app runtime: `plugins/apps/_per_app_runtime.h` owns lifecycle dispatch, the control router state machine (edge detection, 500 ms long-press via `classify_release`, idle reset, `.mask`), the isr cadence accumulator (drives `isr()` at the vendor 16.666 kHz against the NT rate, increments `OC::CORE::ticks`, refreshes `OC::ADC` from and flushes `OC::DAC` to the routed buses), the per-row 32-byte centering shift (vendor `[0,128)` canvas centered to NT `[64,192)`), and the NT-parameter add-on (static `numParameters`, construct-time sentinel guard, `value_names + min` enum offset). `parameterChanged` and routing accessors read the firmware-managed `alg.v`, never the private `v_storage`.

Build model: a per-app TU `#define NT_OC_APP_TU 1`, which makes `_per_app_runtime.h` pull `shim/include/oc_shim_impl.h` (the OC aggregation header, `#include`-ing the shim `.cpp` bodies into the single TU, guarded by the shared `NT_HEM_NO_IMPL` sentinel; the OC analog of `hem_shim_impl.h`, it does NOT pull the Hemisphere-coupled headers). `BUILD_PER_OC_APP` (Makefile) mirrors `BUILD_PER_APPLET`; the host `test_oc_app_%` rule links the aggregating app `.cpp` without `SHIM_CORE_SRCS`. `OC_APP_LIST` enumerates the apps. To add an app: create `plugins/apps/<APP>.cpp` (copy the Low_rents/Harrington1200 pattern: aggregate, build the `OC::App` from the vendor `<PREFIX>_*` static thunks, the `_NT_factory` in vendor field order, the customUI emit-glue) plus `shim/include/oc_app_manifests/<APP>.h`, add it to `OC_APP_LIST` with any `VENDOR_DEPS_<APP>`, and a `harness/tests/test_oc_app_<APP>.cpp`.

customUI emit lives in the per-app `.cpp`, not the runtime: vendor `UI/ui_events.h` puts `Event` in top-level `::UI::` while `OC_apps.h` forward-declares `OC::UI::Event`, so the per-app TU constructs the `::UI::Event` from the runtime's exposed primitives and bridges the App handler pointers with a `reinterpret_cast` (layout-identical structs). The `Low_rents.cpp`/`Harrington1200.cpp` glue is the canonical template.

Two firmware add-time hazards the runtime guards against (both root-caused on hardware; the host harness does not model them, so passing host tests and clean registration do NOT prove a plug-in will ADD): (1) `serialise()` must use `_NT_jsonStream::addNumber` only, never `addString`. The firmware calls `serialise` during add-algorithm, and `addString` faults there (surfaced as "Failed to add algorithm"); `oc_runtime::serialise` packs the `SettingsBase::Save()` blob four bytes per number under `oc_len` / `oc_w0..oc_wN`. (2) The isr cadence loop `while (numerator >= NT_globals.sampleRate)` is guarded with `if (sr == 0) return;` because the firmware runs `step()` at add before the sample rate is set (an unguarded loop spins forever). Always verify a new plug-in ADDS on hardware (`mcp__nt_helper__new` with its GUID), not just that it registers.

Three more on-device gotchas the runtime now handles (all root-caused on hardware, invisible to host tests): (1) the customUI settings push-back (`push_settings_to_params`) must add `NT_parameterOffset()` to the `NT_setParameterFromUi` index, which addresses the GLOBAL parameter table; `inst->v[base+s]` stays plug-in-relative. Omit it and the firmware re-applies the edit to the setting one row above. The sim's offset is 0, so a test must call `nt::set_parameter_offset(1)` to reproduce it. (2) `OC::DAC` values are 16-bit codes: `route_cv_output` maps `(code - 32768) / kCodesPerVolt` to +-5V (0V at the midpoint). Pitch apps reach it through `pitch_to_dac` (1V/oct); modulation apps write raw full-scale codes. Never reintroduce a pitch-only `/1536` output model; it rails full-scale modulation outputs. (3) The screensaver is timed off `OC::CORE::ticks` (25 s, vendor `SCREENSAVER_TIMEOUT_S`), not a draw count, and the firmware footer overlay is suppressed like the hosts (`draw()` caches the bottom 16 rows, `step()` restores them while `steps_since_draw < kFooterRestoreSteps`). Some `APP_*.h` (ENIGMA, MIDI, NeuralNetwork) inherit `HSApplication`, not `OC::App`, and need Teensy usbMIDI/EEPROM/`display.h`; they are out of scope for this foundation.

Two more lessons from the FPART port (`APP_FPART.h`, the first large-settings app, 109 settings). (1) Not every vendor setting can be an NT parameter. `_NT_parameter` min/max/def and `_NT_algorithm::v` are `int16_t` (max 32767). FPART's 99 chord settings are `STORAGE_TYPE_U32` with range up to 32323232, so they cannot be parameters without truncation and corruption. The port exposes only the 10 int16-safe head settings as NT parameters (pass that count, not `<X>_SETTING_LAST`, as `num_settings` to `oc_runtime::construct`); the U32 settings stay app-internal, edited through the customUI and persisted whole through the `Save/Restore` blob. The blob path is independent of `num_settings` (it calls the vendor `SettingsBase::Save`, which packs all settings), so persistence still covers the full table. A large-settings app's real shared blocker is therefore `oc_runtime::kMaxBlobBytes` (the `Save` blob must fit, or `serialise`/`deserialise` bail at the `storage_size() > kMaxBlobBytes` guard and silently drop persistence), not `kMaxSettings`. FPART's blob is 406 bytes; the cap was raised 256 -> 512. (2) A vendor app header may call free functions before their definitions, relying on the Arduino IDE's auto-prototype pass (`FPART_handleButtonEvent` calls `FPART_upButton` and five siblings defined further down `APP_FPART.h`). The shim build has no auto-prototype pass, so the per-app `.cpp` must forward-declare those functions before `#include`-ing the vendor header. The vendor source is not edited.

## Shadowing a vendor header quote-included from inside another vendor header

The established bare-name `-Ishim/include` shadowing works only when the app TU includes the vendor header by bare name (the TU's own directory is `plugins/`, not the vendor tree). When a VENDOR header quote-includes another vendor header (for example `OC_trigger_delays.h` includes `"OC_digital_inputs.h"`, or `APP_H1200.h` includes `"OC_bitmaps.h"`), the compiler searches the including vendor file's directory first and finds the vendor sibling, which `-Ishim/include` cannot override. The fix is include-guard poison: the shim shadow defines the vendor header's own `_H_` include guard (for example `OC_DIGITAL_INPUTS_H_`, `OC_BITMAPS_H_`, `OC_STRINGS_H_`, `UTIL_MATH_H_`), so when the vendor sibling is later pulled it self-suppresses. The shim shadow must then provide every symbol the suppressed vendor body would have. This is how the O_C app foundation shadows `OC_digital_inputs.h`, `OC_bitmaps.h`, `OC_strings.h`, and `util/util_math.h`. The FPART port (`APP_FPART.h`, which quote-includes `OC_apps.h`, `OC_menus.h`, `OC_DAC.h`, `OC_ADC.h`, `OC_strings.h`, `OC_digital_inputs.h` from inside the vendor tree) extended the set: the shim `OC_menus.h`, `OC_apps.h`, `OC_DAC.h`, and `OC_ADC.h` now poison `OC_MENUS_H`, `OC_APP_H_`, `OC_DAC_H_`, and `OC_ADC_H_`. The `OC_menus.h` poison is load-bearing: the vendor `OC_menus.h` hard-includes the Teensy display driver plus vendor `OC_DAC`/`OC_config`/`OC_gpio`, so leaving it unsuppressed pulls the whole non-portable chain.
