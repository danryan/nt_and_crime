# Cleanup release plan

Date: 2026-05-23
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-23-cleanup-brainstorm.md`
Spec: `docs/superpowers/specs/2026-05-23-cleanup-design.md`
Status: draft, awaiting approval
Vendor pins: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Phase outline

Lowest-risk first. Each phase is its own commit (per Conventional Commits rule in the brief: one logical change per commit; bundle commits not allowed).

| Phase | Title | Files | Risk | Parallel? |
|-------|-------|-------|------|-----------|
| A | Deprecated `applets/` + cascade | `applets/*.cpp`, `Makefile`, `docs/hardware-deploy.md`, `harness/tests/test_hemispheres.cpp` (deleted), `harness/tests/applet_test_helpers.{h,cpp}` (deleted), `plugins/applets/ProbabilityDivider.cpp`, `plugins/probes/solo_probe.cpp` | Medium | Sequential |
| B | Superseded shim headers | `shim/include/hemispheres_shim.h`, `HemispheresFactory.h`, `applet_indices.h`, `Empty.h`, `OC_ADC.h` | Low | Sequential (B1 -> B2 -> B3 -> B4; B5 independent) |
| C | Compiler_rt to `vendor/llvm-project` submodule | `.gitmodules`, `bootstrap.sh`, `Makefile`, CLAUDE.md, `shim/src/compiler_rt/` (deleted) | Medium | Sequential |
| D | Docs sprawl consolidation | `docs/superpowers/{brainstorms,specs,plans}/`, `docs/superpowers/archived/` (new) | Low | Parallel by phase |
| E | IWYU sweep | All non-vendor TUs | Low | Deferred (conservative scan: no candidates) |

## Phase A: deprecated `applets/` plus cascade

Sequential. Touches multiple Makefile sections plus the harness test seam.

### A.1 Pre-flight gate

Interpretation A confirmed 2026-05-23. Phase A executes the full cascade (delete `applets/` plus `BUILD_ARM_HEMI_VARIANT` macro pipeline plus variant build outputs). Phase B runs in full after Phase A.

### A.2 Order of commits

1. `chore(harness): drop test_hemispheres binary plus applet_test_helpers (per A2b)`. Deletes `harness/tests/test_hemispheres.cpp` (5157 lines), `harness/tests/applet_test_helpers.h` (564 lines), `harness/tests/applet_test_helpers.cpp` (661 lines). Deletes `build/host/Hemispheres.host.o` rule (`Makefile:392-394`) and `build/host/test_hemispheres` rule (`Makefile:402-404`). Retargets `test-applets:` (`Makefile:406-408`) to alias `test-applets-pilot`. Same commit updates the `CLAUDE.md` "Build and test commands" row for `test-applets` plus the "Architecture" paragraph for `harness/` to remove `test_hemispheres` plus `applet_test_helpers` mentions. Verify `make test-applets` runs the per-applet binaries to completion.
2. `refactor(applets): migrate ProbabilityDivider off hemispheres_shim.h` (per spec A3). Touches `plugins/applets/ProbabilityDivider.cpp` only. Verify `./build/host/test_applet_ProbabilityDivider` green.
3. `refactor(probes): migrate solo_probe off hemispheres_shim.h` (per spec A4, A4=migrate decided 2026-05-23). Replace the `#include "hemispheres_shim.h"` at `plugins/probes/solo_probe.cpp:12` with the direct base set (`HemisphereApplet.h` plus `HSUtils.h` for `LEFT_HEMISPHERE`). Replace `hem_shim::make_applet<APPLET_NAME>(sram)` at line 28 with `new(sram) APPLET_NAME()` (placement new; equivalent). Verify `make build/arm/solo_probe.o` builds.
4a. `chore(applets): remove deprecated Hemispheres.cpp and Hemispheres2.cpp`. Deletes `applets/Hemispheres.cpp`, `applets/Hemispheres2.cpp`, and the now-empty `applets/` directory.
4b. `build(makefile): drop BUILD_ARM_HEMI_VARIANT and bundled-host targets`. Removes `BUILD_ARM_HEMI_VARIANT` macro and its two invocations (`Makefile:239-250`), `VENDOR_DEP_ARM_SRCS`, `VENDOR_DEP_ARM_OBJS`, `VARIANT1_DEP_OBJS`, `VARIANT2_DEP_OBJS` (`Makefile:206-237`), `build/arm/Hemispheres.o` plus `build/arm/Hemispheres2.o` from the `arm:` target (`Makefile:481`), and `Makefile:32` help text. Retargets `SYSEX_PLUGIN ?=` (`Makefile:496`) to `build/arm/Hemispheres_host.o`.
4c. `docs(hardware-deploy): update sysex example to Hemispheres_host.o`. Touches `docs/hardware-deploy.md:7,18`.

### A.3 Verification gates

- After each commit: `make test-applets` plus `make arm`.
- After commit 4: `arm-none-eabi-readelf -W -S build/arm/Hemispheres_host.o` and `build/arm/Quadrants_host.o` `.text` sizes unchanged from baseline (the new hosts are not modified by Phase A).
- After commit 4: `ls build/arm/Hemispheres*.o` lists only `Hemispheres_host.o` (no `Hemispheres.o`, no `Hemispheres2.o`).

### A.4 Halt criteria

- Interpretation A vs B unresolved -> halt, escalate.
- A2 disposition unresolved -> halt, escalate.
- Test count regression beyond what A2 disposition predicts -> halt.

## Phase B: superseded shim headers

Sequential within the cascade (B1 -> B2 -> B3 -> B4); B5 independent and can be the first or last commit.

### B.1 Order of commits

1. `chore(shim): remove orphan OC_ADC.h` (B5). Standalone. Verify `grep -rn 'OC_ADC' shim plugins harness applets` returns zero hits before commit.
2. `chore(shim): remove hemispheres_shim.h` (B1). Gated on Phase A complete; verify zero `grep` hits.
3. `chore(shim): remove HemispheresFactory.h` (B2). Gated on B2 commit; verify zero `grep` hits.
4. `chore(shim): remove applet_indices.h` (B3). Gated on B2 commit; verify zero `grep` hits.
5. `chore(shim): remove Empty.h` (B4). Gated on B2 commit; verify zero `grep` hits.

### B.2 Verification gates

- Per commit: `grep -rn '<header>' plugins harness shim applets vendor/distingNT_API` returns zero before `git rm`.
- After each commit: `make test-applets` plus `make arm`.
- After Phase B: `.text` of `Hemispheres_host.o` plus `Quadrants_host.o` byte-identical to Phase A end-state baseline. Removing unused headers must not change `.text`; deviation means a header was load-bearing in a way the cascade did not surface, and the plan halts.

### B.3 Halt criteria

- Any `grep` returns a non-zero hit -> halt.
- Any `.text` size deviation -> halt.

## Phase C: compiler_rt relocation

Sequential. Per spec "Compiler_rt relocation" section.

### C.1 Order of commits

1. `build(vendor): add llvm-project submodule pinned at llvmorg-19.1.0`. Touches `.gitmodules`, populates `vendor/llvm-project/` via the sparse-checkout block. The submodule is added but not yet consumed by the build.
2. `build(bootstrap): switch make vendor to ./bootstrap.sh + add sparse-checkout block`. Touches `bootstrap.sh` (insert the sparse-checkout block plus retarget the compiler-rt sanity-check loop) and `Makefile` (`vendor:` target line). After this commit the bootstrap script provisions `vendor/llvm-project` correctly in fresh clones and worktrees, but the build still references `shim/src/compiler_rt/`.
3. `refactor(compiler-rt): relocate sources from shim/src/ to vendor/llvm-project`. Touches `Makefile` (the `COMPILER_RT_SRCS` list plus two pattern rules) and deletes `shim/src/compiler_rt/`. CLAUDE.md updates may bundle here OR follow as a separate `docs(claude-md)` commit; recommend separate to keep the refactor commit minimal.
4. `docs: re-point CLAUDE.md NT plug-in runtime and ARM symbol surface sections to vendor/llvm-project`. CLAUDE.md only.

### C.2 Verification gates

- After commit 1: `git submodule status vendor/llvm-project` shows the pinned commit; `ls vendor/llvm-project/compiler-rt/lib/builtins/` lists at least the files the build needs; `ls vendor/llvm-project/llvm/` returns "No such file or directory" (sparse-checkout worked).
- After commit 2: re-run `./bootstrap.sh` in this worktree without errors. Confirm no diff in `vendor/llvm-project` working copy.
- After commit 3: `arm-none-eabi-readelf -W -S build/arm/Hemispheres_host.o` and `Quadrants_host.o` plus every per-applet `.o`: `.text` size byte-identical to the Phase B end-state baseline. Capture before/after side by side.
- After commit 3: `arm-none-eabi-nm build/arm/Hemispheres_host.o | grep __aeabi_d2lz` returns a `T` (defined), not `U` (unresolved).
- Fresh-clone smoke (run once, before opening the PR):

  ```sh
  git clone <repo> /tmp/nt-smoke && cd /tmp/nt-smoke
  ./bootstrap.sh && make vendor && make arm
  test -f vendor/llvm-project/compiler-rt/lib/builtins/fixdfdi.c
  test ! -d vendor/llvm-project/llvm
  ```

- Worktree smoke (run once, before opening the PR):

  ```sh
  git worktree add .worktrees/sparse-smoke -b sparse-smoke
  cd .worktrees/sparse-smoke
  ./bootstrap.sh
  test -f vendor/llvm-project/compiler-rt/lib/builtins/fixdfdi.c
  test ! -d vendor/llvm-project/llvm
  ```

### C.3 Halt criteria

- Any byte-size deviation in `.text` of any `.o` -> halt.
- `__aeabi_d2lz` shows `U` instead of `T` -> halt (compiler-rt source not linking).
- Fresh-clone or worktree smoke pulls the full llvm-project tree (sparse-checkout failed) -> halt.

## Phase D: docs sprawl consolidation

Parallel by phase. Each retired-phase consolidation is independent of the others. Nine consolidations listed in the brainstorm (Phase 1-2 foundation, applet tests track A, Phase 3 attempt 1 plus retry consolidated, Phase 4, Phase 5, Phase 6, per-applet pilot, per-applet mass-port, host UX rework). Dispatchable via subagent fan-out.

### D.1 Parallelism shape

Each implementer subagent handles one retired phase. Output per subagent:

- `docs/superpowers/archived/<phase>/` directory created.
- `git mv` of every doc in the phase's set into that directory.
- `INDEX.md` written per Recipe D.
- `markdownlint` clean on the new `INDEX.md` plus every moved file (no content change to moved files; lint applies to them as-is).
- One commit per phase, scope `docs(superpowers)`.

### D.2 Worktree dispatch checklist (parent agent)

Before dispatching parallel D implementer subagents:

1. Confirm the parent worktree is on the active feature branch (`worktree-dr+cleanup-release` per the existing branch; rename via plan-level commit if a different name is wanted before fan-out). Subagent worktrees branch from this, not from `main`.
2. Each implementer worktree: `git worktree add .worktrees/docs-D<N>-<phase> -b cleanup-docs-<phase> worktree-dr+cleanup-release`.
3. Verify spec docs reachable inside each new worktree: `test -f <wt>/docs/superpowers/specs/2026-05-23-cleanup-design.md`. If missing the dispatch is broken; abort.
4. Submodule provisioning is NOT required for Phase D worktrees (no build runs). Skip `git submodule update --init --recursive --depth=1` for these.
5. Allowed file surface per implementer: `docs/superpowers/archived/<phase>/**` and `docs/superpowers/{brainstorms,specs,plans}/<files moved>`. No other paths.
6. Pre-commit hook check: the existing hook is no-op on the `cleanup-docs-<phase>` branch pattern (none of the case arms match). No additional hook installation needed for Phase D.

### D.3 Per-implementer prompt template

Each subagent receives:

- The retired-phase set of files to move (a sub-list from the brainstorm).
- The target directory under `docs/superpowers/archived/<phase>/`.
- The `INDEX.md` shape per Recipe D in the spec.
- The ship commit SHA to reference (from CLAUDE.md "Phased work and document layout" plus git log).
- Allowed-file surface listed verbatim. "If you stage anything outside this surface, abort."

### D.4 Integration on the parent branch

After all implementer subagents complete:

1. Cherry-pick each implementer's commit onto the feature branch sequentially. Order does not matter because the implementations are file-disjoint.
2. `markdownlint docs/superpowers/archived/*/INDEX.md docs/superpowers/{brainstorms,specs,plans}/*.md` (every active file plus every archived index).
3. `ls docs/superpowers/{brainstorms,specs,plans}/` should show only the active cleanup release docs (`2026-05-23-cleanup-*`) plus the active applet-UX-hardening docs (`2026-05-22-applet-ux-hardening-*`).

### D.5 Halt criteria

- Implementer worktree branched from `main` instead of the feature branch -> abort that worktree, re-dispatch from feature branch.
- `markdownlint` errors uncovered in moved files -> fix in the integration commit before opening PR. Moved files preserve content; lint failures should be pre-existing.

## Phase E: IWYU sweep (deferred 2026-05-23)

Conservative heuristic scan run against all four directories found zero unused-include candidates that would survive the byte-identical `.text` gate. The scan checked each `#include` for matching symbol-name references in its containing TU; zero hits across `shim/include/`, `shim/src/`, `plugins/`, `harness/`.

The negative result is meaningful: per-applet plug-ins, host plug-ins, and shim sources all carry tight include lists because they were authored under the mass-port spec which already constrains the include set. A deeper semantic IWYU pass (resolving transitive macro and namespace dependencies, e.g. `OC::Strings::*` consumed via `OC_strings.h`) is possible but high-effort per file and orthogonal to the cleanup release's scope.

Phase E is deferred to a follow-up release that does dedicated semantic IWYU. No Phase E commits land in the cleanup release.

Original Phase E specification (kept for reference):

Parallel by directory. Four directories: `shim/include/`, `shim/src/`, `plugins/`, `harness/`. One implementer subagent per directory.

### E.1 Per-implementer scope

Per directory the implementer:

1. Walks every `.h` and `.cpp` in the directory.
2. For each file: identify `#include` lines that no name in the TU references.
3. Removes the unused `#include` only if `make` for that directory's matching target still passes.
4. Commits as `refactor(<scope>): drop unused includes in <dir>` where `<scope>` matches the directory's Conventional Commits scope (`shim`, `plugins`, `harness`).

### E.2 Parallelism caveat

Headers in `shim/include/` are included by every other directory. An IWYU sweep in `shim/include/` runs first; the other three sweeps follow in parallel after the shim sweep lands.

Sequencing:

1. Shim include sweep (sequential, in place on the feature branch).
2. Shim src + plugins + harness sweeps (parallel, three implementers).

### E.3 Verification gates

- Per commit: matching `make` target for the directory builds clean.
- After all four: `make test-applets` plus `make arm` plus `make test`. `.text` of `Hemispheres_host.o`, `Quadrants_host.o`, every per-applet `.o` byte-identical to Phase D end-state baseline.

### E.4 Halt criteria

- Any `.text` size deviation -> halt; an include was load-bearing in a non-obvious way.
- Any test regression -> halt.

## Pre-commit hook content

The existing hook at `.git/hooks/pre-commit` (full content recorded in CLAUDE.md "Parallel execution" section) is a no-op on the cleanup release branch (`worktree-dr+cleanup-release` plus `cleanup-*` subagent branches do not match any case arm). No new hook content added. Phase A is sequential and direct-executed on the feature branch; subagent surface guarding is unnecessary.

## Integration verification per phase

After each phase's last commit:

- `make test-applets`: passes (TEST_CASE count predicted by phase-specific verification gates).
- `make arm`: clean. Lists the expected set of plug-ins (Phase A drops two from `make arm`'s implicit set; later phases do not change the list).
- `make test`: passes (host build plus applet tests plus scripted scenario).
- `arm-none-eabi-readelf -W -S build/arm/Hemispheres_host.o`: capture before/after.
- `arm-none-eabi-readelf -W -S build/arm/Quadrants_host.o`: capture before/after.

Aggregate at PR open:

- `markdownlint docs/superpowers/**/*.md README.md CLAUDE.md docs/hardware-deploy.md docs/shim-additions.md`: clean.

## Fresh-clone smoke gate (Phase C only)

Run on a clean machine state (or a fresh `/tmp` clone) after Phase C's last commit, before opening the PR:

```sh
cd /tmp
rm -rf nt-smoke
git clone /opt/code/github.com/danryan/nt_and_crime nt-smoke
cd nt-smoke
git checkout worktree-dr+cleanup-release
./bootstrap.sh
make vendor
make arm
test -f vendor/llvm-project/compiler-rt/lib/builtins/fixdfdi.c
test ! -d vendor/llvm-project/llvm
arm-none-eabi-nm build/arm/Hemispheres_host.o | grep __aeabi_d2lz
```

Expected: all commands succeed; `nm` lists `__aeabi_d2lz` with type `T`.

## Worktree smoke gate (Phase C only)

Run inside the cleanup release worktree after Phase C's last commit:

```sh
git worktree add .worktrees/sparse-smoke -b sparse-smoke worktree-dr+cleanup-release
cd .worktrees/sparse-smoke
./bootstrap.sh
test -f vendor/llvm-project/compiler-rt/lib/builtins/fixdfdi.c
test ! -d vendor/llvm-project/llvm
cd ..
git worktree remove --force .worktrees/sparse-smoke
git branch -D sparse-smoke
```

Expected: bootstrap completes; sparse-checkout populated `compiler-rt/lib/builtins/` only; the rest of `vendor/llvm-project/` absent.

## Hardware smoke gate (at PR open)

After all five phases land:

1. Deploy each new host plug-in: `make deploy-sysex SYSEX_PLUGIN=build/arm/Hemispheres_host.o SYSEX_ID=0`, then same for `Quadrants_host.o`.
2. Build a preset with each host visible: Hemispheres + 2 applets in slots 1-2; Quadrants + 4 applets in slots 1-4.
3. Cycle through every per-applet plug-in by swapping the enum selector on each host. Confirm no regressions.
4. Verify `Misc > Plug-ins > View Info` shows no Failed entries for the per-applet plug-ins, `Hemispheres_host.o`, or `Quadrants_host.o`.

Hardware smoke is gated on Dan's physical access. PR is opened with the gate status marked "pending hardware smoke" in the description.

## Conventional Commits manifest

One commit per logical change. All commits follow `<type>(<scope>): <subject>`. Examples per phase:

Phase A:

- `chore(harness): drop test_hemispheres binary plus applet_test_helpers`
- `refactor(applets): migrate ProbabilityDivider off hemispheres_shim.h`
- `refactor(probes): migrate solo_probe off hemispheres_shim.h`
- `chore(applets): remove deprecated Hemispheres.cpp and Hemispheres2.cpp`
- `build(makefile): drop BUILD_ARM_HEMI_VARIANT and bundled-host targets`
- `docs(hardware-deploy): update sysex example to Hemispheres_host.o`

Phase B:

- `chore(shim): remove orphan OC_ADC.h`
- `chore(shim): remove hemispheres_shim.h`
- `chore(shim): remove HemispheresFactory.h`
- `chore(shim): remove applet_indices.h`
- `chore(shim): remove Empty.h`

Phase C:

- `build(vendor): add llvm-project submodule pinned at llvmorg-19.1.0`
- `build(bootstrap): switch make vendor to bootstrap.sh + sparse-checkout`
- `refactor(compiler-rt): relocate sources from shim/src/ to vendor/llvm-project`
- `docs: re-point CLAUDE.md compiler-rt sections to vendor/llvm-project`

Phase D (one per archived phase):

- `docs(superpowers): consolidate phase 1-2 foundation docs into archived/`
- `docs(superpowers): consolidate applet-tests-track-a docs into archived/`
- `docs(superpowers): consolidate phase 3 docs into archived/`
- `docs(superpowers): consolidate phase 4 docs into archived/`
- `docs(superpowers): consolidate phase 5 docs into archived/`
- `docs(superpowers): consolidate phase 6 docs into archived/`
- `docs(superpowers): consolidate per-applet pilot docs into archived/`
- `docs(superpowers): consolidate per-applet mass-port docs into archived/`
- `docs(superpowers): consolidate host UX rework docs into archived/`

Phase E:

- Deferred to follow-up release. Conservative IWYU scan found no candidates across the four directories. See Phase E section above.

Total: roughly 18 commits. Each is single-purpose; no bundles.

## PR shape

Title: `chore: cleanup release (deprecated applets/, compiler_rt to vendor, docs archive, IWYU)`

Body (per global git rules):

- Summary: four bullets covering Phase A, B, C, D, E roll-up.
- Test plan: markdown checkboxes for `make test-applets`, `make arm`, `make test`, byte-identical `.text` sizes verified, fresh-clone smoke, worktree smoke, hardware smoke pending.
- Links: brainstorm, spec, plan paths.

## Stop and request approval

Plan complete. Per the brief: do not start fan-out, do not start any code change, do not run `git rm` until the user reviews and approves the brainstorm, spec, and plan.

All outstanding decisions resolved 2026-05-23:

- Interpretation A: drop `applets/` plus the entire `BUILD_ARM_HEMI_VARIANT` pipeline. See brainstorm "Scope decision" section.
- A2b: delete `test_hemispheres.cpp`, seam, host build rules; retarget `test-applets` to alias `test-applets-pilot`. See brainstorm "Test seam deletion" section.
- A4=migrate: replace `hemispheres_shim.h` plus `hem_shim::make_applet<>` in `solo_probe.cpp` with direct base includes plus placement-new.
- Optional pre-commit hook: omitted. Phase A sequential plus direct-executed; subagent guard unnecessary.

After approval the plan moves to fan-out under `superpowers:subagent-driven-development` for the parallelizable phases (D, E) and direct execution for the sequential phases (A, B, C).
