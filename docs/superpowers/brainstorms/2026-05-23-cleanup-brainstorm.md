# Cleanup release brainstorm

Date: 2026-05-23
Owner: Dan
Status: draft, awaiting approval

## Vendor pins

- `vendor/O_C-Phazerville` at `7800d929f25868f9a8b7d3d50514532ee001649b`
- `vendor/distingNT_API` at `cd12d876dbe060859828053efab1cbc98c9df251`

Confirmed via `git ls-tree HEAD vendor/O_C-Phazerville vendor/distingNT_API`.

## Baseline (green)

- `make test-applets`: 2596 assertions in 282 test cases, all pass.
- `make arm`: clean build of all 55 per-applet plug-ins, both bundled hosts (`Hemispheres.o`, `Hemispheres2.o`), both new hosts (`Hemispheres_host.o`, `Quadrants_host.o`), and the four diagnostic probes.
- `arm-none-eabi-readelf -W -S build/arm/Hemispheres.o`: `.text` = `0xf5d0` (62928 bytes).
- `arm-none-eabi-readelf -W -S build/arm/Hemispheres2.o`: `.text` = `0xc2b0` (49840 bytes).

These two values are the regression gate for the compiler_rt relocation phase. Same source plus same flags must produce byte-identical objects.

## Scope decision (resolved 2026-05-23)

Interpretation A confirmed by Dan. The brief's "HEMI_VARIANT variant logic out of scope" reads as "do not rebalance variant membership inside `HemispheresFactory.h`"; the build pipeline that consumes the deleted files goes away as a natural consequence of the cleanup release ending the bundled-host transition.

Cascade scope under interpretation A:

- `applets/Hemispheres.cpp`, `applets/Hemispheres2.cpp` deleted.
- `BUILD_ARM_HEMI_VARIANT` macro (`Makefile:239-247`) plus its two invocations (`Makefile:249-250`) deleted.
- `VENDOR_DEP_ARM_SRCS`, `VENDOR_DEP_ARM_OBJS`, `VARIANT1_DEP_OBJS`, `VARIANT2_DEP_OBJS` (`Makefile:206-237`) deleted.
- `Hemispheres.o` plus `Hemispheres2.o` removed from the `arm:` target prerequisite list (`Makefile:481`).
- `build/host/Hemispheres.host.o` rule (`Makefile:392-394`) deleted.
- `SYSEX_PLUGIN` default (`Makefile:496`) retargeted to `build/arm/Hemispheres_host.o`.
- `Makefile:32` help text plus `docs/hardware-deploy.md:7,18` updated to match.

Migration cost for users: bundled `"hemi"` plus `"hmi2"` GUIDs disappear. Existing presets referencing those GUIDs stop loading. Users rebuild presets on the new `Hemispheres_host.o` plus `Quadrants_host.o` (GUIDs `HmHh` plus `QdHh`). The new hosts compose per-applet plug-ins at runtime; bundled distribution is no longer offered.

Original ambiguity (for audit trail):

The brief listed `applets/Hemispheres.cpp` and `applets/Hemispheres2.cpp` as greenlit removal AND listed `HEMI_VARIANT` split membership and variant logic as out of scope. These constraints conflicted at the Makefile boundary.

Evidence the bundled host machinery and the deprecated `applets/` directory are coupled:

- `Makefile:239-247` defines `BUILD_ARM_HEMI_VARIANT` which compiles `applets/$(1).cpp` into `build/arm/$(1).o`.
- `Makefile:249-250` invokes it for `Hemispheres` (variant 1) and `Hemispheres2` (variant 2). Each consumes the matching `.cpp` in `applets/`.
- `Makefile:392` rule `build/host/Hemispheres.host.o` consumes `applets/Hemispheres.cpp` for the host test binary.
- `Makefile:402` rule `build/host/test_hemispheres` depends on `Hemispheres.host.o`.
- `Makefile:481` lists `Hemispheres.o` and `Hemispheres2.o` as direct prerequisites of the `arm` target.
- `Makefile:496` sets `SYSEX_PLUGIN ?= build/arm/Hemispheres.o` as the deploy-sysex default.
- `docs/hardware-deploy.md:7,18` documents the same default.

Removing `applets/*.cpp` requires removing the ARM build for the bundled hosts AND the host test binary that links the same TU. The reading consistent with the rest of the brief is interpretation A:

- A: variant-membership rebalancing is out of scope but the bundled-host build path goes away as a natural consequence of the cleanup release ending the transition (per `docs/superpowers/prompts/2026-05-19-per-applet-refactor-kickoff.md:220` "Bundled (retained for transition)" framing). The cleanup release deletes `applets/` and the entire `BUILD_ARM_HEMI_VARIANT` pipeline together; the new hosts at `plugins/hosts/Hemispheres_host.cpp` and `plugins/hosts/Quadrants_host.cpp` remain as the supported composition path.

- B: variant logic stays; defer `applets/` removal to a later release.

Interpretation A matches `docs/superpowers/brainstorms/2026-05-19-per-applet-pilot-brainstorm.md:133` ("The cleanup-release deletion of `applets/Hemispheres.cpp` plus `applets/Hemispheres2.cpp` is out of scope for the pilot release") and the README. The plan below assumes A. The spec and plan call this out explicitly so the reviewer can flip to B before fan-out if desired.

If interpretation A is wrong, the bundled-host removal phase (Phase A below) shrinks to deleting only the `applets/` content and the plan halts there. The brief lists no candidate that is dependent on Phase A surviving in full.

## Dead-code inventory

### Confirmed orphans

- `shim/include/OC_ADC.h` (3 lines, full content: `#pragma once` and `enum { ADC_CHANNEL_LAST = 4 };`). Zero `#include` references anywhere in the tree. The single hit `shim/include/CVInputMap.h:32` is a comment (`// ADC_CHANNEL_LAST from OC_ADC.h`) re-defining the constant locally, not an include. Confirmed via `grep -rn 'OC_ADC' shim plugins harness applets vendor/distingNT_API`.

### Conditionally orphaned (depend on Phase A interpretation A)

These files are live today because they back the bundled `Hemispheres.o` + `Hemispheres2.o` build path. They become orphans once that path is deleted:

- `applets/Hemispheres.cpp` (13 lines). `NT_HEMISPHERES_PLUGIN` macro plus `hem_test::get_applet_impl` test seam. Test seam is declared in `harness/tests/applet_test_helpers.h:50` and used by `harness/tests/test_hemispheres.cpp` only.
- `applets/Hemispheres2.cpp` (14 lines). `NT_HEMISPHERES_PLUGIN` for variant 2.
- `shim/include/hemispheres_shim.h`. Includers in the live tree:
  - `applets/Hemispheres.cpp:1` (Phase A removes).
  - `applets/Hemispheres2.cpp:11` (Phase A removes).
  - `plugins/probes/solo_probe.cpp:12` (preserve per brief; needs migration to direct includes or stays as-is if compatible with stand-alone build).
  - `plugins/applets/ProbabilityDivider.cpp:5` (mass-port leftover; the mass-port spec at `docs/superpowers/specs/2026-05-20-per-applet-mass-port-design.md` declares `Do NOT include hemispheres_shim.h`; this one TU is non-conformant).
- `shim/include/HemispheresFactory.h`. Single non-self includer: `shim/include/hemispheres_shim.h:4`. Becomes orphan when `hemispheres_shim.h` does.
- `shim/include/applet_indices.h`. Single non-self includer: `shim/include/HemispheresFactory.h:5`. Becomes orphan when `HemispheresFactory.h` does.
- `shim/include/Empty.h`. Single non-self includer: `shim/include/HemispheresFactory.h:40`. Used as the `make_applet<Empty>` stub for applets dropped from a variant (e.g. `HemispheresFactory.h:355,369-411`). Becomes orphan when `HemispheresFactory.h` does.

If interpretation B is chosen, none of the above leave the tree in this release.

### Test seam deletion (A2b confirmed 2026-05-23)

Decision: delete everything tied to the original bundled Hemispheres plug-in, including the harness host-test binary that drove it. A2b chosen over A2a.

Scope:

- `harness/tests/test_hemispheres.cpp` (5157 lines, 282 TEST_CASEs, 2596 assertions) deleted.
- `harness/tests/applet_test_helpers.h` (564 lines) deleted.
- `harness/tests/applet_test_helpers.cpp` (661 lines) deleted.
- `build/host/Hemispheres.host.o` rule (`Makefile:392-394`) deleted.
- `build/host/test_hemispheres` rule (`Makefile:402-404`) deleted.
- `test-applets:` Makefile target (`Makefile:406-408`) retargeted to depend on `test-applets-pilot` (`Makefile:417-419`). The verb `make test-applets` is preserved as an alias for per-applet test runs; the body switches.

Verified via grep: `applet_test_helpers.{h,cpp}` is consumed only by `test_hemispheres.cpp:5`. No `test_applet_*.cpp` includes the helper header. Per-applet tests reimplement pack helpers locally per the pattern documented in `harness/tests/test_applet_Cumulus.cpp:31-32`, `harness/tests/test_applet_Relabi.cpp:12,39`, `harness/tests/test_applet_VectorLFO.cpp:64-65`, `harness/tests/test_applet_ClockDivider.cpp:122-127`. Helper file deletion is safe.

Coverage delta: 282 TEST_CASEs go. Per-applet tests at 508 TEST_CASEs across 55 files become the only applet coverage. Cross-applet bus-interaction cases in `test_hemispheres.cpp` are not migrated. User accepts the loss.

`hem_shim::HemispheresInstance` becomes unreferenced after this commit and `applets/Hemispheres.cpp` removal. Falls out as part of Phase B (`hemispheres_shim.h` removal).

`hem_shim::inner_ticks_override` is retained (used by per-applet tests `test_applet_Shuffle.cpp`, `test_applet_ResetClock.cpp`, `test_applet_ShiftGate.cpp`; Phase 5 time-injection helper, not bundled-host-specific).

CLAUDE.md update: "Build and test commands" table row for `make test-applets` retargets to "Build and run Catch2 host tests for per-applet plug-ins (alias for `test-applets-pilot`)". The "harness/" paragraph under "Architecture" updates accordingly: `harness/tests/test_hemispheres.cpp` removed from the description; `harness/tests/test_applet_*.cpp` becomes the canonical applet test surface.

## Near-duplicate inventory (cleanup category 3)

Surveyed and found no candidates worth collapsing in this release.

- `shim/include/lorenz/streams_*.h` versus `shim/src/lorenz/streams_*.h`: the headers in `shim/src/lorenz/` are six-line forwarding bridges (`#include "lorenz/<file>"`) required by the vendor `.cpp` files because they use bare-name includes. Intentional and non-redundant. Confirmed by reading both pairs.
- `pack_<applet>` helpers in `harness/tests/applet_test_helpers.{h,cpp}`: 58 helpers, one per applet that defines `OnDataRequest`. Each mirrors a different vendor pack layout byte-for-byte per the documented CLAUDE.md rule. Not a duplication candidate; the brief explicitly says do not collapse unless the dupe is true.
- `Hemispheres_host.cpp` versus `Quadrants_host.cpp`: brief flags these as a deferred-tier candidate. See "Deferred to follow-up" D3.
- Vendored copies of vendor source (lorenz cpp pair plus quant header trio): true duplication identified but deferred to a follow-up release. See "Deferred to follow-up" D6.

## Multiple-ways-to-do-the-same-thing inventory (cleanup category 4)

Surveyed and found no candidates worth unifying in this release.

- Two host plug-in shapes: bundled (`applets/Hemispheres.cpp` plus `applets/Hemispheres2.cpp`) versus composer (`plugins/hosts/Hemispheres_host.cpp` plus `plugins/hosts/Quadrants_host.cpp`). Two ways to load applets onto the device. Phase A's cleanup retires the bundled path; only the composer path survives. Post-cleanup, this category becomes "one way".
- Two applet TU shapes: the bundled host pulls applets in via `HemispheresFactory.h` macro expansion (compile-time inclusion); per-applet plug-ins under `plugins/applets/` are independent TUs loaded via `HemiPluginInterface`. Phase A removes the bundled side, leaving only the per-applet shape.
- One TU bypass within the per-applet world: `plugins/applets/ProbabilityDivider.cpp:5` includes `hemispheres_shim.h` whereas the other 54 per-applet TUs follow the canonical mass-port include set. Single TU coming into compliance (handled under Phase A item A3); not a category 4 candidate at scale.
- Test runner duality: `make test-applets` (bundled host test binary) versus `make test-applets-pilot` (per-applet binaries). Phase A retargets `test-applets` to alias `test-applets-pilot`; the duality collapses to a single test runner with two invocation names.
- No other "two ways to do X" patterns surfaced in the survey. Diagnostic probes share a single shape; vendor dep ports share a single shape (header in `shim/include/<dep>/`, cpp in `shim/src/<dep>/`); applet manifests share a single shape under `shim/include/applet_manifests/`.

## Misplaced-vendor-source audit

### Confirmed misplaced (target of Structural fix)

`shim/src/compiler_rt/` contains 16 files copied verbatim from llvm-project tag `llvmorg-19.1.0` (Apache 2.0 with LLVM exception):

| File | Lines | Source path in llvm-project |
|------|-------|------------------------------|
| `divdi3.c` | 22 | `compiler-rt/lib/builtins/divdi3.c` |
| `divmoddi4.c` | 28 | `compiler-rt/lib/builtins/divmoddi4.c` |
| `fixdfdi.c` | 48 | `compiler-rt/lib/builtins/fixdfdi.c` |
| `fixunsdfdi.c` | 46 | `compiler-rt/lib/builtins/fixunsdfdi.c` |
| `udivdi3.c` | 23 | `compiler-rt/lib/builtins/udivdi3.c` |
| `udivmoddi4.c` | 200 | `compiler-rt/lib/builtins/udivmoddi4.c` |
| `assembly.h` | 293 | `compiler-rt/lib/builtins/assembly.h` |
| `fp_lib.h` | 416 | `compiler-rt/lib/builtins/fp_lib.h` |
| `int_endianness.h` | 114 | `compiler-rt/lib/builtins/int_endianness.h` |
| `int_lib.h` | 171 | `compiler-rt/lib/builtins/int_lib.h` |
| `int_math.h` | 108 | `compiler-rt/lib/builtins/int_math.h` |
| `int_types.h` | 276 | `compiler-rt/lib/builtins/int_types.h` |
| `int_util.h` | 47 | `compiler-rt/lib/builtins/int_util.h` |
| `int_div_impl.inc` | (not counted) | `compiler-rt/lib/builtins/int_div_impl.inc` |
| `arm/aeabi_div0.c` | (not counted) | `compiler-rt/lib/builtins/arm/aeabi_div0.c` |
| `arm/aeabi_ldivmod.S` | 45 | `compiler-rt/lib/builtins/arm/aeabi_ldivmod.S` |
| `arm/aeabi_uldivmod.S` | 45 | `compiler-rt/lib/builtins/arm/aeabi_uldivmod.S` |

Violates the project convention recorded in CLAUDE.md: "Vendor source is pinned via git submodules and never edited. Everything project-specific lives in `shim/` and `applets/`." Target state: relocate under a new `vendor/llvm-project` submodule pinned at `llvmorg-19.1.0` with sparse-checkout limited to `compiler-rt/lib/builtins/`.

### Audit hit, NOT misplaced

`shim/src/lorenz/streams_resources.cpp` and `shim/src/lorenz/streams_lorenz_generator.cpp` come from `vendor/O_C-Phazerville` (already a submodule). The header copies in `shim/include/lorenz/` are full vendor ports plus shim preamble; the `.cpp` files apply the same preamble pattern at `shim/src/lorenz/`. These are not raw verbatim vendor sources; they carry shim-specific adapters and live under `shim/src/` correctly. The forwarding-bridge headers in `shim/src/lorenz/` exist because the vendor `.cpp` files use bare-name `#include "streams_*.h"`. CLAUDE.md's "Vendor dep cpps must link into ARM plug-in" guidance treats them as live shim adapters.

No relocation. Not folded into the compiler_rt structural fix.

## Docs sprawl inventory

Required-reading per CLAUDE.md (preserve verbatim, do not edit):

- `docs/superpowers/abort-reports/2026-05-18-phase3-attempt-1-retrospective.md`
- `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md`

Active workstream docs (preserve verbatim; the applet-UX-hardening phase has not finished and the cleanup release does not bundle with it):

- `docs/superpowers/brainstorms/2026-05-22-applet-ux-hardening-brainstorm.md`
- `docs/superpowers/plans/2026-05-22-applet-ux-hardening-plan.md`

Live kickoff prompts (preserve verbatim; the workflow references them by name):

- `docs/superpowers/prompts/2026-05-18-phase5-deps-kickoff.md` (CLAUDE.md "Phased work and document layout" line cites this).
- `docs/superpowers/prompts/2026-05-19-per-applet-refactor-kickoff.md`
- `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md`
- `docs/superpowers/prompts/2026-05-20-per-applet-mass-port-kickoff.md`
- `docs/superpowers/prompts/2026-05-18-phase6.md`
- `docs/superpowers/prompts/2026-05-18-phase5-kickoff.md` (superseded by deps-kickoff but historically referenced).

Retired phase docs (consolidate via per-phase index file; preserve archived originals under `docs/superpowers/archived/<phase>/`):

- Phase 1 + Phase 2 foundation: `plans/2026-05-16-plan-a-harness-gate.md`, `plans/2026-05-16-plan-b-shim-and-logic.md`, `plans/2026-05-17-plan-c-tier1-applets.md`, `plans/2026-05-17-plan-d-bitmap-icon-fidelity.md`, `plans/2026-05-17-plan-e-multi-applet-pair.md`, `plans/2026-05-17-plan-f-hemispheres-runtime-applet-selector.md`, `plans/2026-05-17-plan-g-tier2-applet-expansion.md`, `specs/2026-05-16-nt-hemisphere-shim-design.md`.
- Applet tests track A: `plans/2026-05-17-applet-tests-track-a-phase1.md`, `plans/2026-05-17-applet-tests-track-a-phase2.md`, `plans/2026-05-17-applet-tests-track-a-phase2-parallel.md`, `specs/2026-05-17-applet-tests-track-a-design.md`.
- Phase 3 attempt 1 plus retry: `brainstorms/2026-05-17-phase3-applets-brainstorm.md`, `brainstorms/2026-05-18-phase3-retry-brainstorm.md`, `specs/2026-05-18-phase3-applets-design.md`, `specs/2026-05-18-phase3-retry-design.md`, `plans/2026-05-18-phase3-applets-plan.md`, `plans/2026-05-18-phase3-retry-plan.md`. (Abort reports stay where they are.)
- Phase 4: `brainstorms/2026-05-18-phase4-applets-brainstorm.md`, `specs/2026-05-18-phase4-applets-design.md`, `plans/2026-05-18-phase4-applets-plan.md`.
- Phase 5: `brainstorms/2026-05-18-phase5-deps-brainstorm.md`, `specs/2026-05-18-phase5-deps-design.md`, `plans/2026-05-18-phase5-deps-plan.md`.
- Phase 6: `brainstorms/2026-05-18-phase6-applets-brainstorm.md`, `specs/2026-05-18-phase6-applets-design.md`, `plans/2026-05-18-phase6-applets-plan.md`.
- Per-applet pilot: `brainstorms/2026-05-19-per-applet-pilot-brainstorm.md`, `specs/2026-05-19-per-applet-pilot-design.md`, `plans/2026-05-19-per-applet-pilot-plan.md`.
- Per-applet mass-port: `brainstorms/2026-05-20-per-applet-mass-port-brainstorm.md`, `specs/2026-05-20-per-applet-mass-port-design.md`, `plans/2026-05-20-per-applet-mass-port-plan.md`.
- Host UX rework: `brainstorms/2026-05-21-host-ux-rework-brainstorm.md`, `specs/2026-05-21-host-ux-rework-design.md`, `plans/2026-05-21-host-ux-rework-plan.md`.

Strategy: do not delete. Move each retired bundle into `docs/superpowers/archived/<phase>/` preserving filenames, then write one `docs/superpowers/archived/<phase>/INDEX.md` per phase summarizing scope, ship commit, and where to find the originals. The top-level `docs/superpowers/{brainstorms,specs,plans}/` directories keep only active and forthcoming material.

## Unused includes

Survey not done at full IWYU depth. The IWYU-style sweep is part of Phase F and runs after the bulk deletions land so the sweep does not chase moving targets. No specific candidates pre-identified.

## Category status

| Category | Confidence | Phase |
|----------|-----------|-------|
| `shim/include/OC_ADC.h` | confirmed orphan | A |
| `applets/` dir plus cascade (`hemispheres_shim.h`, `HemispheresFactory.h`, `applet_indices.h`, `Empty.h`, bundled-host Makefile macro, `test_hemispheres` seam relocation) | conditional on interpretation A | A |
| `ProbabilityDivider.cpp` migration off `hemispheres_shim.h` | confirmed needed if A | A |
| compiler_rt relocation | confirmed needed | C |
| Docs archive consolidation | confirmed needed | E |
| Near-duplicate collapse (cat 3) | none found | (skip) |
| Multiple-ways unification (cat 4) | none found | (skip) |
| Unused includes IWYU sweep | not pre-identified | F |

## Exclusions named

- `vendor/` submodule contents.
- Applet runtime behavior.
- Shim contract for vendor applet headers under `vendor/O_C-Phazerville/software/src/applets/*.h`.
- `HEMI_VARIANT` variant-membership balancing within `HemispheresFactory.h` (the macro pipeline goes away under interpretation A; the membership inside the factory table is not rebalanced).
- `*_test_inject_slot` host-test seam structure.
- Pre-commit hook structure.
- Diagnostic probes under `plugins/probes/` (all five stay).

## Deferred to follow-up

Capture-only. Do not act in this release. Each candidate names an observation, file:line pointers, and the followup work that would answer the question.

### D1. NT simulator retained

- Decision: keep `harness/src/nt_runtime.cpp` and the `make test-runtime` target. No action this release; no followup audit needed.
- Rationale: simulator exercises code paths hardware smoke does not (deterministic seeded RNG, controlled bus injection, the 10x clocked multiplier modeled explicitly per CLAUDE.md "Critical gotcha: 10x clocked multiplier"). Hardware smoke is necessarily coarse; simulator covers the fine grain.
- Pointers: `harness/src/nt_runtime.cpp`, `harness/include/nt_runtime.h`, `harness/tests/test_nt_runtime.cpp`, `Makefile` `test-runtime` target.

### D2. Per-applet pack header layout

- Observation: 58 `pack_<applet>` helpers in one TU pair (`applet_test_helpers.cpp` 661 lines, `applet_test_helpers.h` 564 lines). Mass-port release moved every other applet artifact to per-applet files; the test pack layer did not follow.
- Pointers: `harness/tests/applet_test_helpers.{h,cpp}`; one example per-applet user `harness/tests/test_applet_Cumulus.cpp`.
- Followup option (no duplication): split into `harness/tests/applet_packs/<APPLET>.h` per applet, header-only. Each `test_applet_<APPLET>.cpp` includes only its own; `test_hemispheres.cpp` includes the full set. Shared definitions stay shared via include, not via copy. Edit surface for a pack change drops to one small file.
- Cost vs benefit: 58 small headers plus 58 include lines in `test_hemispheres.cpp` versus two monolithic files. Worth doing if a follow-up cleanup ever has a non-test reason to touch the layer.
- Default: keep monolithic until a concrete trigger surfaces. Simplicity over speculative locality.

### D3. Hemispheres_host plus Quadrants_host duplication

- Status: deferred. No action this release.
- Observation: `plugins/hosts/Hemispheres_host.cpp` and `plugins/hosts/Quadrants_host.cpp` follow nearly the same shape (host_proxy aggregator, customUi forwarding, footer overdraw cache, parameter proxying). Differences are slot count (2 versus 4) and a few configuration constants.
- Pointers: `plugins/hosts/Hemispheres_host.cpp`, `plugins/hosts/Quadrants_host.cpp`, `shim/include/host_proxy.h`, `shim/src/host_proxy.cpp`.
- Followup: code-side diff to enumerate the actual deltas. If the delta is constants-only, the followup is a single `BUILD_HOST_PLUGIN(name, slot_count, ...)` macro. If structural, leave separate.

### D5. Probe load-bearing audit

- Status: deferred. Keep all five probes in tree.
- Observation: CLAUDE.md lists all five probes as load-bearing. Some (e.g. `reentrancy_probe`) tested a specific firmware bug that may have been fixed in a later firmware release.
- Pointers: `plugins/probes/aeabi_probe.cpp`, `plugins/probes/bus_probe.cpp`, `plugins/probes/reentrancy_probe.cpp`, `plugins/probes/section_probe.cpp`, `plugins/probes/solo_probe.cpp`. CLAUDE.md "Construct-time parameterChanged hazard" describes the reentrancy result.
- Followup: per-probe audit re-deploying each on current firmware and confirming the observed behavior still matches the documented finding. Audit is hardware-gated.

### D6. Vendor de-duplication for lorenz and quant

- Observation: lorenz and quant carry verbatim copies of vendor source. The split (`shim/include/` plus `shim/src/`) with forwarding bridges exists to support these copies; collapsing the duplication likely collapses the split friction with it.
- Evidence:
  - Lorenz: all four files byte-identical to vendor. Confirmed via `diff -q` against `vendor/O_C-Phazerville/software/src/streams_{lorenz_generator,resources}.{h,cpp}`. `shim/include/lorenz/streams_lorenz_generator.h` (140 lines) plus `shim/include/lorenz/streams_resources.h` (105 lines) plus `shim/src/lorenz/streams_lorenz_generator.cpp` (231 lines) plus `shim/src/lorenz/streams_resources.cpp` (1437 lines). Plus `shim/src/lorenz/streams_*.h` (12 lines, 2 bridge headers). Total 1925 lines of duplication.
  - Quant headers: three of three byte-identical to vendor. `shim/include/quant/braids_quantizer.h` (123 lines), `braids_quantizer_scales.h` (353 lines), `OC_scales.h` (73 lines). Total 549 lines of duplication. `shim/include/quant/MIDIQuantizer.h` (34 lines) is shim invention; not duplication.
  - Quant cpps: legitimate divergence. `shim/src/quant/braids_quantizer.cpp` versus vendor diffs include: dropped `OC_options.h` + `util/util_misc.h` includes (no shim equivalent), added `util/util_macros.h`, explicit field initialization (`num_notes_=0`, `span_=0`, `note_number_=0`, `requantize_=false`; vendor relies on caller to set these), removed `#ifdef NORTHERNLIGHT` branches (NT is not Northernlight; vendor compiles correctly with the flag undefined anyway, so the explicit removal is cosmetic), and a `constexpr` to `static constexpr` change on one local. `OC_scales.cpp` carries similar divergence; not exhaustively read.
- Pointers: `shim/include/lorenz/streams_*.h`, `shim/src/lorenz/streams_*.{h,cpp}`, `shim/include/quant/{braids_quantizer,braids_quantizer_scales,OC_scales}.h`, `shim/src/quant/{braids_quantizer,OC_scales,q_engine}.cpp`, `Makefile:206-208,399-400` (`VENDOR_DEP_ARM_SRCS`, `VENDOR_DEP_HOST_SRCS`).
- Followup release scope: dedicated vendor de-duplication release. Three candidate paths for the quant cpps:
  - Path 1: provide shim stubs for `OC_options.h` and `util/util_misc.h`; drop `shim/src/quant/*.cpp`; build vendor cpps verbatim. NORTHERNLIGHT undefined naturally, so the `#ifdef` branches resolve correctly. Field-init divergence requires a separate audit: vendor bug fix versus shim convention.
  - Path 2: keep shim cpps; drop only the header duplicates (549 lines). Lower risk, smaller win.
  - Path 3: upstream the NORTHERNLIGHT cleanup plus include trims to vendor. Loses control; depends on vendor merge cadence.
- Why not this release:
  - Touches `VENDOR_DEP_ARM_SRCS` plus `VENDOR_DEP_HOST_SRCS` build paths, the same Makefile region as Phase A. Phase A is already medium-risk plus Makefile-heavy. Bundling raises blast radius.
  - Quant cpp divergence needs its own audit (field-init: vendor bug fix or shim convention?). Cannot answer in plan-time.
  - Verification gate is byte-identical `.text` on `LowerRenz.o` (lorenz consumer) plus every quant-using applet. Same gate shape as Phase C compiler_rt. Worth a dedicated sweep rather than a ride-along.
- Estimated savings: roughly 2474 lines of duplication plus the two bridge headers plus two Makefile rule entries. Plus ~171 more if quant cpps go via Path 1.
- Risk distribution per path: lorenz drop low; quant header drop low; quant cpp drop medium.

## Working notes

- Vendor pin verification command: `git ls-tree HEAD vendor/O_C-Phazerville vendor/distingNT_API` (run successfully today, results recorded above).
- Hardware smoke check is gated at PR open per project convention. Not part of plan-level verification.
- The brief commits to a Conventional Commits format (`<type>(<scope>): <subject>`) and explicitly disallows `fix:` or `BREAKING CHANGE:` in this release. If a cleanup uncovers a real bug, halt and escalate rather than expanding scope.
