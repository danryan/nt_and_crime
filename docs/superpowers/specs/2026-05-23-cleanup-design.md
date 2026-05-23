# Cleanup release spec

Date: 2026-05-23
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-23-cleanup-brainstorm.md`
Status: draft, awaiting approval
Vendor pins: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Scope summary

Five cleanup categories, sequenced lowest-risk first:

- A: confirmed orphan + deprecated `applets/` directory + cascade (under interpretation A from the brainstorm).
- B: superseded shim files made orphan by Phase A.
- C: relocate vendored `compiler_rt/` sources into a new `vendor/llvm-project` submodule, sparse-checkout limited to `compiler-rt/lib/builtins/`.
- D: docs sprawl consolidation into `docs/superpowers/archived/<phase>/` per-phase indexes.
- E: unused-include IWYU sweep run last so it does not chase moving targets.

Out of scope per brief: `vendor/` submodule contents (adding a new submodule is in scope), applet runtime behavior, shim contract for vendor applet headers, `HEMI_VARIANT` membership inside `HemispheresFactory.h` (the build pipeline that consumes it goes away under interpretation A; the table itself is not rebalanced before deletion), `*_test_inject_slot` seam structure, pre-commit hook structure, diagnostic probes.

## Canonical recipe per cleanup category

Recipes apply to every per-item entry under their category.

### Recipe A: deprecated-directory removal plus cascade

Steps:

1. Identify includers via `grep -rn '<header>' plugins/ harness/ shim/ applets/`.
2. For each live includer, migrate the source TU to the per-applet pattern (mass-port spec: `docs/superpowers/specs/2026-05-20-per-applet-mass-port-design.md`).
3. Remove `#include` plus delete the file. Verify by `make test-applets` plus `make arm`.
4. Update Makefile to drop matching rules and prerequisite lists.
5. Update `docs/hardware-deploy.md` plus `Makefile` help text for any user-facing default that changes (e.g. `SYSEX_PLUGIN` default).

Verify steps:

- `make test-applets` passes (`2596 assertions in 282 test cases` baseline; the actual count after relocation depends on test seam disposition).
- `make arm` produces all per-applet plug-ins plus the two new hosts plus the diagnostic probes. The bundled `Hemispheres.o` plus `Hemispheres2.o` no longer build (intentional under interpretation A).
- `make test` runs the full host build plus applet tests plus scripted scenario.
- `arm-none-eabi-readelf -W -S build/arm/Hemispheres_host.o` and `Quadrants_host.o` `.text` sizes are unchanged from baseline.

Rollback:

- `git revert` the cleanup commit. The bundled `applets/` and Makefile macro return verbatim. No state is shared with the per-applet build path so revert is mechanical.

### Recipe B: superseded shim removal

Steps:

1. After Phase A lands, run `grep -rn '<header>' plugins/ harness/ shim/` for each conditional-orphan header.
2. Expect zero hits. If a hit remains, halt and escalate (the cascade did not finish or a hidden includer survived).
3. `git rm` the orphan file.
4. `make test-applets` plus `make arm`.

Verify steps:

- Zero grep hits before removal.
- `make test-applets` green.
- `make arm` produces identical `Hemispheres_host.o` + `Quadrants_host.o` `.text` sizes versus the Phase A end-state baseline.

Rollback:

- `git revert`. Files were unreferenced so the revert restores them without functional change.

### Recipe C: vendor relocation

See dedicated section below ("Compiler_rt relocation"). Recipe is large enough to warrant its own walkthrough.

### Recipe D: docs archive consolidation

Steps:

1. Create `docs/superpowers/archived/<phase>/` per phase listed in the brainstorm.
2. `git mv` each retired-phase doc into its archived directory, preserving filenames.
3. Write `docs/superpowers/archived/<phase>/INDEX.md` per phase:
   - One-paragraph scope summary.
   - Ship commit SHA reference.
   - Bulleted list of files now in the directory with one-line each.
   - Pointer to any retained kickoff prompt that still lives in `docs/superpowers/prompts/`.
4. Run `markdownlint docs/superpowers/archived/<phase>/INDEX.md` plus each moved file.

Verify steps:

- `ls docs/superpowers/{brainstorms,specs,plans}/` contains only active workstream files (applet UX hardening from 2026-05-22 plus the cleanup release docs from 2026-05-23).
- `git log --oneline -- docs/superpowers/archived/` shows the move commit per phase.
- `markdownlint` clean on all `.md` files in `docs/superpowers/`.

Rollback:

- `git revert` the per-phase move commit. `git mv` history preserved.

### Recipe E: IWYU sweep

Steps:

1. Walk every `.cpp` and `.h` under `shim/include/`, `shim/src/`, `plugins/`, `harness/` (skip `vendor/` and `build/`).
2. For each, identify `#include` lines that no name in the TU references. Use grep, not blind tooling (the brief is explicit: "IWYU-style reasoning, not blind tools").
3. Remove the unused `#include` only if the file still builds.

Verify steps:

- Per-file: build the matching test or ARM target after each removal batch (group by directory).
- Aggregate: `make test-applets` plus `make arm` at the end.
- Aggregate: `arm-none-eabi-readelf -W -S build/arm/Hemispheres_host.o` and `Quadrants_host.o` byte-identical to Phase D end-state. Includes do not change behavior; if `.text` shifts something else moved.

Rollback:

- `git revert` the offending commit. Each commit is one logical sweep, so the revert is targeted.

## Per-item entries

### Phase A items

A1. `applets/Hemispheres.cpp` plus `applets/Hemispheres2.cpp` removal.

- Action: delete both files. Remove `applets/` directory.
- Cascade in same commit: delete `BUILD_ARM_HEMI_VARIANT` macro (`Makefile:239-247`), its two invocations (`Makefile:249-250`), `VENDOR_DEP_ARM_SRCS` (`Makefile:206-207`) plus `VENDOR_DEP_ARM_OBJS` (`Makefile:208`), `VARIANT1_DEP_OBJS` (`Makefile:236`), `VARIANT2_DEP_OBJS` (`Makefile:237`), the `arm:` prerequisite entries `build/arm/Hemispheres.o` plus `build/arm/Hemispheres2.o` (`Makefile:481`), the `build/host/Hemispheres.host.o` rule (`Makefile:392-394`), the `build/host/test_hemispheres` rule (`Makefile:402-404`), the `.PHONY: test-applets` plus `test-applets` rule entry (`Makefile:406-408`) if `test-applets` is being preserved (see A4 disposition).
- Files touched: `applets/Hemispheres.cpp`, `applets/Hemispheres2.cpp`, `Makefile`, `docs/hardware-deploy.md`.
- Update `Makefile:496` `SYSEX_PLUGIN ?=` default away from `build/arm/Hemispheres.o`. Recommended new default: `build/arm/Hemispheres_host.o` (the new bundled composer that replaces the deprecated bundled set).
- Update `Makefile:32` help text similarly.
- Update `docs/hardware-deploy.md:7,18` to reference `Hemispheres_host.o` rather than `Hemispheres.o` as the deploy-sysex default.
- Verification: `make test-applets`, `make arm`, `make test`. The two bundled `.o` files no longer build; the per-applet plus host plug-ins continue.
- Risk: medium. Several Makefile sections touched.

A2. `harness/tests/test_hemispheres.cpp` deletion (A2b, decided 2026-05-23).

- Action: delete `harness/tests/test_hemispheres.cpp` in full. Delete `hem_test::get_applet_impl` declaration plus the inline `get_applet` wrapper at `harness/tests/applet_test_helpers.h:52-58`. Delete the `build/host/Hemispheres.host.o` rule (`Makefile:392-394`) and the `build/host/test_hemispheres` rule (`Makefile:402-404`). Retarget the `test-applets:` target (`Makefile:406-408`) to depend on `test-applets-pilot` so `make test-applets` continues to work as an alias for the per-applet test run.
- Files touched: `harness/tests/test_hemispheres.cpp` (deleted), `harness/tests/applet_test_helpers.h` (seam declaration plus wrapper removed; pack helpers retained), `Makefile` (three rules edited as above), `CLAUDE.md` ("Build and test commands" table row for `test-applets`; "Architecture" paragraph for `harness/`).
- Coverage delta: 282 TEST_CASEs plus 2596 assertions removed. Per-applet suite (508 TEST_CASEs across 55 files) becomes the only applet coverage. Cross-applet bus-interaction cases are not migrated; the user accepts the loss.
- Verification: `make test-applets` builds (now an alias for `test-applets-pilot`) and runs all 55 per-applet test binaries to completion. `grep -rn 'test_hemispheres' Makefile harness/ docs/` returns zero hits after the commit.
- Risk: low to medium. The deletion is mechanical; the residual risk is that a hidden cross-applet regression slips past the per-applet suite. No way to mitigate inside the cleanup release.

A3. `plugins/applets/ProbabilityDivider.cpp` migration off `hemispheres_shim.h`.

- Action: remove `#include "../../shim/include/hemispheres_shim.h"` at `plugins/applets/ProbabilityDivider.cpp:5`. Replace with the canonical per-applet include list per the mass-port spec (`shim/include/applet_manifests/ProbabilityDivider.h` plus the per-applet runtime header).
- Files touched: `plugins/applets/ProbabilityDivider.cpp` (include list only; no code change).
- Verification: `make build/host/test_applet_ProbabilityDivider` builds clean. `./build/host/test_applet_ProbabilityDivider` passes.
- Risk: low. The other 54 per-applet plug-ins already follow the canonical pattern; this is one TU coming into compliance.

A4. `plugins/probes/solo_probe.cpp` migration (A4=migrate decided 2026-05-23).

- Read confirmed solo_probe does not depend on the bundled `HemispheresInstance` shape. It uses `hem_shim::make_applet<APPLET_NAME>(sram)` (templated factory from `HemispheresFactory.h`) plus `LEFT_HEMISPHERE` (from `HS::` namespace) plus the `HemisphereApplet` base class virtuals.
- Action: replace `#include "hemispheres_shim.h"` at `solo_probe.cpp:12` with the direct base set (`HemisphereApplet.h` plus `HSUtils.h` for `LEFT_HEMISPHERE`; possibly additional headers depending on what `make_applet` was transitively pulling). Replace `hem_shim::make_applet<APPLET_NAME>(sram)` at line 28 with `new(sram) APPLET_NAME()` (placement new; semantically identical to the factory template).
- Files touched: `plugins/probes/solo_probe.cpp`.
- Verification: `make build/arm/solo_probe.o` builds clean.
- Risk: low. Probe is diagnostic; behavior unchanged.

### Phase B items (Phase A interpretation A only)

B1. `shim/include/hemispheres_shim.h` removal.

- Action: `git rm shim/include/hemispheres_shim.h`. Gated on zero non-self includers after Phase A.
- Verification: `grep -rn 'hemispheres_shim' plugins harness shim applets` returns zero. Build full ARM. `.text` of `Hemispheres_host.o` plus `Quadrants_host.o` unchanged.

B2. `shim/include/HemispheresFactory.h` removal.

- Action: `git rm shim/include/HemispheresFactory.h`. Gated on B1.
- Verification: as B1 but for `HemispheresFactory.h`.

B3. `shim/include/applet_indices.h` removal.

- Action: `git rm shim/include/applet_indices.h`. Gated on B2.
- Verification: as B1 but for `applet_indices.h`.

B4. `shim/include/Empty.h` removal.

- Action: `git rm shim/include/Empty.h`. Gated on B2.
- Verification: as B1 but for `Empty.h`.

B5. `shim/include/OC_ADC.h` removal.

- Action: `git rm shim/include/OC_ADC.h`. Standalone; not gated.
- Verification: `grep -rn 'OC_ADC' shim plugins harness applets vendor/distingNT_API` returns zero. The single comment hit at `shim/include/CVInputMap.h:32` may be updated to drop the dangling reference (one-character edit: replace the `OC_ADC.h` reference with `CVInputMap.h`'s own constant comment).

### Phase C item

C1. compiler_rt relocation under new `vendor/llvm-project` submodule. See "Compiler_rt relocation" section below.

### Phase D items

D1-D9. Per-phase docs archive consolidation. Nine consolidations listed in the brainstorm (Phase 1-2 foundation, applet tests track A, Phase 3 attempt 1 plus retry consolidated, Phase 4, Phase 5, Phase 6, per-applet pilot, per-applet mass-port, host UX rework). Action per phase: see Recipe D.

### Phase E item

E1. IWYU sweep across `shim/`, `plugins/`, `harness/`. Per-file `#include` audit. One commit per directory sweep to keep the revert surface narrow.

## Compiler_rt relocation

### Target state

- New submodule at `vendor/llvm-project` pinned at `llvmorg-19.1.0`, sparse-checkout limited to `compiler-rt/lib/builtins/`.
- `shim/src/compiler_rt/` directory deleted in full.
- Makefile points at `vendor/llvm-project/compiler-rt/lib/builtins/<file>.{c,S}` for every compiler_rt source.
- Build rule constraints unchanged: `.c` compiled with `arm-none-eabi-gcc`, not `c++`; partial-link via `ld -r --strip-debug`.
- CLAUDE.md "NT plug-in runtime" plus "ARM unresolved-symbol surface" sections re-point at new paths.
- `bootstrap.sh` sparse-checkout block runs idempotently in fresh clones plus in worktrees.

### `.gitmodules` diff

Add this entry (the existing entries stay in place):

```text
[submodule "vendor/llvm-project"]
        path = vendor/llvm-project
        url = https://github.com/llvm/llvm-project.git
        branch = llvmorg-19.1.0
        shallow = true
```

### `bootstrap.sh` diff

Replace the current compiler-rt sanity-check block (`bootstrap.sh:88-117`) with the sparse-checkout block. The PIC libgcc warning block (`bootstrap.sh:119-130`) is unchanged.

Insertion (after the Catch2 amalgamation block at line 86):

```text
# vendor/llvm-project sparse-checkout: compiler-rt/lib/builtins only.
# The default `git submodule update --init --recursive --depth=1` does NOT
# carry sparse-checkout config, so the bootstrap must apply it explicitly.
# Block is idempotent: safe to re-run in fresh clones and in worktrees.
SUBMOD=vendor/llvm-project
URL=$(git config -f .gitmodules submodule.$SUBMOD.url)
TAG=$(git config -f .gitmodules submodule.$SUBMOD.branch)

if [ ! -e "$SUBMOD/.git" ]; then
    git clone --no-checkout --depth=1 --filter=blob:none \
        -b "$TAG" "$URL" "$SUBMOD"
    git -C "$SUBMOD" sparse-checkout init --cone
    git -C "$SUBMOD" sparse-checkout set compiler-rt/lib/builtins
    git -C "$SUBMOD" checkout "$TAG"
    git submodule absorbgitdirs "$SUBMOD"
else
    git -C "$SUBMOD" sparse-checkout reapply
fi

# compiler-rt sanity check: file presence under the new submodule path.
COMPILER_RT_SOURCES=(
    vendor/llvm-project/compiler-rt/lib/builtins/divdi3.c
    vendor/llvm-project/compiler-rt/lib/builtins/udivdi3.c
    vendor/llvm-project/compiler-rt/lib/builtins/divmoddi4.c
    vendor/llvm-project/compiler-rt/lib/builtins/udivmoddi4.c
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_div0.c
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_ldivmod.S
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_uldivmod.S
    vendor/llvm-project/compiler-rt/lib/builtins/int_lib.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_types.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_util.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_endianness.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_div_impl.inc
    vendor/llvm-project/compiler-rt/lib/builtins/assembly.h
)
COMPILER_RT_MISSING=()
for f in "${COMPILER_RT_SOURCES[@]}"; do
    [ -f "$f" ] || COMPILER_RT_MISSING+=("$f")
done
if [ ${#COMPILER_RT_MISSING[@]} -gt 0 ]; then
    echo "bootstrap: missing compiler-rt sources under vendor/llvm-project (required for make arm):" >&2
    for m in "${COMPILER_RT_MISSING[@]}"; do echo "  - $m" >&2; done
    echo "Confirm sparse-checkout config: git -C vendor/llvm-project sparse-checkout list" >&2
    exit 1
fi
```

The trailing PIC libgcc multilib sanity check (`bootstrap.sh:119-130`) stays put. Update its trailing message to point at the new path:

- Before: `compiler-rt vendoring remains the preferred path.`
- After: `compiler-rt sourcing from vendor/llvm-project remains the preferred path.`

### Makefile diff

Replace `COMPILER_RT_SRCS` (`Makefile:174-183`) and adjust two pattern rules (`Makefile:188-190` for `.c`, `Makefile:198-200` for `.S`):

```text
COMPILER_RT_SRCS := \
    vendor/llvm-project/compiler-rt/lib/builtins/divdi3.c \
    vendor/llvm-project/compiler-rt/lib/builtins/udivdi3.c \
    vendor/llvm-project/compiler-rt/lib/builtins/divmoddi4.c \
    vendor/llvm-project/compiler-rt/lib/builtins/udivmoddi4.c \
    vendor/llvm-project/compiler-rt/lib/builtins/fixdfdi.c \
    vendor/llvm-project/compiler-rt/lib/builtins/fixunsdfdi.c \
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_div0.c \
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_ldivmod.S \
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_uldivmod.S
COMPILER_RT_OBJS := \
    $(patsubst vendor/llvm-project/compiler-rt/lib/builtins/%.c,build/arm/compiler_rt/%.o,$(filter %.c,$(COMPILER_RT_SRCS))) \
    $(patsubst vendor/llvm-project/compiler-rt/lib/builtins/%.S,build/arm/compiler_rt/%.o,$(filter %.S,$(COMPILER_RT_SRCS)))

build/arm/compiler_rt/%.o: vendor/llvm-project/compiler-rt/lib/builtins/%.c
        mkdir -p $(@D)
        $(ARM_CC) $(ARM_CFLAGS) -Ivendor/llvm-project/compiler-rt/lib/builtins -c -o $@ $<

build/arm/compiler_rt/%.o: vendor/llvm-project/compiler-rt/lib/builtins/%.S
        mkdir -p $(@D)
        $(ARM_CC) $(ARM_CFLAGS) -Ivendor/llvm-project/compiler-rt/lib/builtins -c -o $@ $<
```

The two `-I` flags (line 190 and line 200) move from `-Ishim/src/compiler_rt` to `-Ivendor/llvm-project/compiler-rt/lib/builtins`. No other rule references the directory.

Also update the `vendor:` target (`Makefile:503-504`):

```text
vendor:
        ./bootstrap.sh
```

The bootstrap script is the canonical entry point because it carries the sparse-checkout block. The current `git submodule update --init --recursive` line is insufficient (it does not apply sparse config).

### CLAUDE.md diff

Two paragraphs to retarget. Both currently reference `shim/src/compiler_rt/`:

- "NT plug-in runtime" section: replace "vendor compiler-rt builtins under `shim/src/compiler_rt/` (verbatim from llvm-project tag `llvmorg-19.1.0`)" with "use compiler-rt builtins from `vendor/llvm-project/compiler-rt/lib/builtins/` (submodule pinned at `llvmorg-19.1.0`, sparse-checkout limited to that tree)".
- "ARM unresolved-symbol surface (firmware contract)" section: replace "compiler_rt `fixdfdi.c` + `fixunsdfdi.c` + `fp_lib.h` + `int_math.h` vendored under `shim/src/compiler_rt/`" with "compiler_rt `fixdfdi.c` + `fixunsdfdi.c` + `fp_lib.h` + `int_math.h` from `vendor/llvm-project/compiler-rt/lib/builtins/`".

### Verification

Required gates before declaring C1 done:

1. Byte-identical objects: `arm-none-eabi-readelf -W -S build/arm/Hemispheres_host.o` plus `Quadrants_host.o` plus every per-applet `.o` matches the Phase B end-state baseline exactly. Same source plus same flags must produce identical objects; any deviation means the relocation altered behavior and the plan halts. Capture `.text`, `.rodata`, `.data`, `.bss` sizes pre and post.
2. Symbol resolution preserved: `arm-none-eabi-nm build/arm/Hemispheres_host.o | grep __aeabi_d2lz` returns a defined symbol (lowercase `T`), not `U` (unresolved).
3. Fresh-clone smoke:

   ```sh
   git clone <repo> /tmp/nt-smoke && cd /tmp/nt-smoke
   ./bootstrap.sh && make vendor && make arm
   test -f vendor/llvm-project/compiler-rt/lib/builtins/fixdfdi.c
   test ! -d vendor/llvm-project/llvm   # sparse worked
   ```

4. Worktree smoke:

   ```sh
   git worktree add .worktrees/sparse-smoke -b sparse-smoke
   cd .worktrees/sparse-smoke
   ./bootstrap.sh
   test -f vendor/llvm-project/compiler-rt/lib/builtins/fixdfdi.c
   test ! -d vendor/llvm-project/llvm
   ```

### Risk and rollback

Risk: medium. The build pipeline depends on these files. Sparse-checkout must survive fresh-clone and worktree-provisioning. If sparse config does not apply, the submodule pulls the full llvm-project tree (~hundreds of MB) and the failure is loud (disk usage), not silent.

Rollback: `git revert` the relocation commit. The old `shim/src/compiler_rt/` content was deleted in the same commit; the revert restores it. The `vendor/llvm-project` submodule entry can stay in `.gitmodules` (a `git submodule deinit vendor/llvm-project` cleans up the working copy) or be reverted in a follow-up commit. The Makefile and CLAUDE.md edits revert mechanically.

## Spec footer

### Recipe spot-check

Recipe A walkthrough using A3 (`ProbabilityDivider.cpp` migration off `hemispheres_shim.h`):

1. `grep -rn 'hemispheres_shim' plugins harness shim applets` -> four hits (Hemispheres.cpp, Hemispheres2.cpp, solo_probe.cpp, ProbabilityDivider.cpp). The first two go away in A1; solo_probe is audited in A4; the fourth is this entry.
2. Migrate `ProbabilityDivider.cpp` includes to the per-applet pattern (see `plugins/applets/Compare.cpp:1-8` for the canonical example).
3. `make build/host/test_applet_ProbabilityDivider && ./build/host/test_applet_ProbabilityDivider` -> green.
4. No Makefile change needed; the per-applet build rule already covers this TU.
5. No doc change needed; the TU was non-conformant against an existing spec.

Recipe applies cleanly.

### Per-entry verification

Per-item entries A1-A4, B1-B5, C1, D1-D11, E1 each list verification commands and rollback steps. The commands are concrete (executable as written). Each item is one logical change; commits are single-purpose per the Conventional Commits rules in the brief.

### Shim prereq verification

The cleanup release does not add new vendor headers, does not modify the shim contract for vendor applet headers, and does not introduce new shim machinery. The only shim-prereq question is whether the conditional orphans (B1-B5) are reachable from a vendor applet header at all. Confirmed no: vendor applet headers under `vendor/O_C-Phazerville/software/src/applets/*.h` include `HSUtils.h`, `HSicons.h`, `HSIOFrame.h`, `OC_core.h`, `HemisphereApplet.h`. None of those touch `HemispheresFactory.h`, `applet_indices.h`, `Empty.h`, `hemispheres_shim.h`, or `OC_ADC.h`. The shim contract is preserved unchanged.
