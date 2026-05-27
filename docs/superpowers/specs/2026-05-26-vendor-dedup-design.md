# Vendor de-duplication for lorenz and quant: design

Date: 2026-05-26
Tracks: GitHub issue #20
Brainstorm: `docs/superpowers/brainstorms/2026-05-26-vendor-dedup-brainstorm.md`
Vendor pin: `vendor/O_C-Phazerville` at SHA `7800d929f25868f9a8b7d3d50514532ee001649b`

## Scope

Ship Unit 1 (lorenz) and Unit 2 (quant headers). Defer Unit 3 (quant cpps) to
a follow-up issue. The brainstorm records the divergence audit that drives the
defer.

## Correctness contract

The artifact is byte-identical `.text`, not merely a clean build. The de-dup
is correct if and only if every affected ARM object's `.text` section is
identical before and after, and the host test and scenario suites still pass.

## Canonical recipe

Each unit follows the same procedure. The unit of work is "replace a verbatim
copy with the vendor original without moving a byte of `.text`."

1. Baseline. Before any edit, build `make arm` and record the `.text` hash of
   every `build/arm/*.o`:

   ```sh
   make arm
   for o in build/arm/*.o; do
     arm-none-eabi-objcopy -O binary --only-section=.text "$o" /tmp/t.text
     printf '%s  %s\n' "$(shasum -a 256 /tmp/t.text | cut -d" " -f1)" "$(basename "$o")"
   done | sort -k2 > /tmp/text-baseline.txt
   ```

2. Drop the copy. Delete the shim copy (and any bridge headers it forced).

3. Wire the vendor source. Edit the Makefile so the build reads vendor source
   instead of the deleted copy. Edit include sites to bare names where the
   vendor layout requires it.

4. Rebuild. `make arm`.

5. Confirm. Recompute the `.text` hash set and diff against baseline:

   ```sh
   for o in build/arm/*.o; do
     arm-none-eabi-objcopy -O binary --only-section=.text "$o" /tmp/t.text
     printf '%s  %s\n' "$(shasum -a 256 /tmp/t.text | cut -d" " -f1)" "$(basename "$o")"
   done | sort -k2 > /tmp/text-after.txt
   diff /tmp/text-baseline.txt /tmp/text-after.txt && echo "TEXT IDENTICAL"
   ```

   A non-empty diff means the copy and the vendor source were not identical at
   build time. Halt and investigate; do not accept the new artifact.

6. Behavioral gate. `make test-applets`, `make arm`, and `make test` pass.

7. Commit (one unit per commit) or revert.

## Shared enabler (carried in Unit 1, relied on by Unit 2)

Both units require `vendor/O_C-Phazerville/software/src` (the Makefile variable
`HEM_SRC_DIR`) on the include path so that bare-name includes of vendor headers
resolve. This is a single edit:

```make
SHIM_INCLUDE := -Ishim/include -I$(HEM_SRC_DIR)
```

`-Ishim/include` stays first so shim stubs continue to shadow any same-named
vendor header (`OC_core.h`, `Arduino.h`, `util/util_misc.h`, and similar). The
edit only adds a search path; because shim is first and everything compiles
today, no existing first-match resolution changes. The Layer-0 verification
(step 1 baseline, then this edit alone, then step 5) must show TEXT IDENTICAL
across all of `make arm` before any deletion. If adding the path alone moves
any `.text` hash, a vendor header is shadowing a shim stub; halt.

Because both units edit `SHIM_INCLUDE`, they are not independently parallel as
the kickoff assumed. They are lightly sequenced: Unit 1 carries the
`SHIM_INCLUDE` edit (it needs it for `test_dep_lorenz`), and Unit 2 builds on
it. The wallclock cost of sequencing two small commits is negligible.

## Unit 1: lorenz

Files deleted (4 copies plus 2 bridges):

- `shim/include/lorenz/streams_lorenz_generator.h`
- `shim/include/lorenz/streams_resources.h`
- `shim/src/lorenz/streams_lorenz_generator.cpp`
- `shim/src/lorenz/streams_resources.cpp`
- `shim/src/lorenz/streams_lorenz_generator.h` (bridge)
- `shim/src/lorenz/streams_resources.h` (bridge)

Why the bridges disappear: the bridges exist only because the copy split each
cpp (`shim/src/lorenz/`) from its header (`shim/include/lorenz/`). The vendor
cpps sit in `software/src/` next to their headers, so building them in place
resolves the bare `#include "streams_*.h"` via the compiler's own-directory
search for quote-includes. No bridge and no extra include flag is needed for
the vendor objects themselves.

Makefile edits:

- Apply the shared `SHIM_INCLUDE` enabler above.
- Add an ARM object rule that compiles the vendor cpps in place:

  ```make
  build/arm/vendor_src/%.o: $(HEM_SRC_DIR)/%.cpp
  	mkdir -p $(@D)
  	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) -c -o $@ $<
  ```

- Repoint `VENDOR_DEPS_LowerRenz` (Makefile:291) to the new objects:

  ```make
  VENDOR_DEPS_LowerRenz := build/arm/vendor_src/streams_resources.o \
                           build/arm/vendor_src/streams_lorenz_generator.o
  ```

- Repoint `VENDOR_DEP_HOST_SRCS` (Makefile:354) to vendor sources:

  ```make
  VENDOR_DEP_HOST_SRCS := $(HEM_SRC_DIR)/streams_resources.cpp \
                          $(HEM_SRC_DIR)/streams_lorenz_generator.cpp
  ```

Include repoint:

- `harness/tests/test_dep_lorenz.cpp:17`: change `#include
  "lorenz/streams_lorenz_generator.h"` to bare `#include
  "streams_lorenz_generator.h"`. It resolves via the shared `-I$(HEM_SRC_DIR)`.
  Update the explanatory comment block (lines 5-8) so it no longer describes
  the deleted forwarding bridges.

Affected ARM object: `build/arm/LowerRenz.o` is the only applet that links the
streams code, so it is the only object whose `.text` could move. The full
`make arm` hash diff still runs (the `SHIM_INCLUDE` edit touches every build),
and the expectation is the entire set is identical.

Host and dep coverage: `test_dep_lorenz`, the LowerRenz per-applet test, and
`make test` exercise the relocated source.

## Unit 2: quant headers

Files deleted (3 headers):

- `shim/include/quant/braids_quantizer.h`
- `shim/include/quant/braids_quantizer_scales.h`
- `shim/include/quant/OC_scales.h`

Kept (shim-invented, not duplicates):

- `shim/include/quant/MIDIQuantizer.h`
- `shim/src/quant/q_engine.cpp`
- `shim/src/quant/braids_quantizer.cpp` (Unit 3, deferred)
- `shim/src/quant/OC_scales.cpp` (Unit 3, deferred)

Include repoints to bare names (all rely on the shared `-I$(HEM_SRC_DIR)`):

| Site | From | To |
| --- | --- | --- |
| `shim/include/HSUtils.h:12` | `"quant/OC_scales.h"` | `"OC_scales.h"` |
| `shim/src/quant/braids_quantizer.cpp:5` | `"quant/braids_quantizer.h"` | `"braids_quantizer.h"` |
| `shim/src/quant/q_engine.cpp:4` | `"quant/OC_scales.h"` | `"OC_scales.h"` |
| `shim/src/quant/OC_scales.cpp:5` | `"quant/OC_scales.h"` | `"OC_scales.h"` |
| `harness/tests/test_dep_quant.cpp:13` | `"quant/OC_scales.h"` | `"OC_scales.h"` |

`shim/include/HSMIDI.h:252` keeps `#include "quant/MIDIQuantizer.h"` unchanged;
MIDIQuantizer.h stays under `shim/include/quant/`.

Preserve the diagnostic pragmas around the HSUtils include. Lines 10-13 wrap
the include in `#pragma GCC diagnostic ignored "-Waddress"` to silence a
tautological NULL check inside vendor `braids_quantizer.h`. The wrapper stays;
only the include path changes. Update the comment at HSUtils.h:6 so it reads
`pulled in via OC_scales.h` rather than the stale `quant/OC_scales.h`.

Internal includes inside the vendor headers (`OC_scales.h` includes bare
`braids_quantizer.h` and `braids_quantizer_scales.h`; `braids_quantizer_scales.h`
includes bare `braids_quantizer.h`) resolve in the vendor source directory once
the build points there. No edit needed; the deleted shim headers had identical
internal includes.

Coupling to deferred Unit 3: the kept shim quant cpps
(`braids_quantizer.cpp`, `OC_scales.cpp`, `q_engine.cpp`) are in the repoint
table above. Changing their include lines is behavior-preserving because the
vendor header content is byte-identical to the deleted shim header.

Affected ARM objects: every applet `.o` that includes `HSUtils.h` plus both
hosts. The gate is the full `make arm` `.text` hash diff; the expectation is
the entire set is identical.

## Out of scope

- `vendor/` (read-only submodules)
- `build/`
- any behavior change or any `.text` change
- upstreaming to vendor (issue Path 3)
- broader `shim/include/` versus `shim/src/` reorganization
- Unit 3 quant cpps (follow-up issue)

## Commits

Conventional Commits, footer `Refs: #20`. Two commits:

- `refactor(shim): build vendor lorenz source directly, drop copies`
  (carries the shared `SHIM_INCLUDE` enabler)
- `refactor(shim): drop duplicated quant headers, include vendor directly`

## Spec footer

### Recipe spot-check

Walk the canonical recipe once on `shim/include/quant/OC_scales.h` (Unit 2):

1. Baseline `make arm`, hash all `build/arm/*.o` `.text`.
2. Delete `shim/include/quant/OC_scales.h`.
3. `HSUtils.h:12` already repointed to bare `OC_scales.h`; `-I$(HEM_SRC_DIR)`
   on `SHIM_INCLUDE` resolves it to `vendor/.../software/src/OC_scales.h`,
   which is byte-identical to the deleted file.
4. `make arm`.
5. Hash diff against baseline. Identical preprocessed input plus identical
   flags yields identical `.text`. Expect TEXT IDENTICAL.
6. `make test-applets`, `make test` pass.
7. Commit.

The recipe is self-consistent: it gates on the artifact, not the build, and the
revert path is explicit.

### Per-entry verification

Three entries traced end-to-end against vendor source at SHA `7800d929`.

1. Unit 1, `shim/src/lorenz/streams_resources.cpp`. `diff -q` against
   `software/src/streams_resources.cpp` reports identical. The cpp includes
   bare `"streams_resources.h"` (line 33), which after the move resolves to the
   sibling `software/src/streams_resources.h`. Traces clean.

2. Unit 2, `shim/include/quant/braids_quantizer.h`. `diff -q` against
   `software/src/braids_quantizer.h` reports identical. Its consumers reach it
   transitively through `OC_scales.h`. Traces clean.

3. Unit 3 control, `shim/src/quant/braids_quantizer.cpp`. `diff` against
   `software/src/braids_quantizer.cpp` reports DIFFERS: shim adds
   `num_notes_/span_/note_number_/requantize_` initialization in `Init()` that
   vendor omits and `Process()` reads. Confirms Unit 3 is correctly excluded.

Zero of three in-scope entries contradict the clean-win classification; the
one control entry confirms the defer. No full re-audit triggered.

### Shim prerequisite verification

- Vendor submodule provisioned in this worktree and checked out at the pinned
  SHA `7800d929` (verified against the gitlink before diffing).
- `diff -q` re-confirmed byte-identical for all four lorenz files and all three
  quant headers at the pinned SHA.
- `arm-none-eabi-objcopy`, `arm-none-eabi-c++`, and `shasum` available (the
  ARM build runs today).
- `markdownlint` available for the document gate.
- `SHIM_INCLUDE`, `VENDOR_DEPS_LowerRenz`, `VENDOR_DEP_HOST_SRCS`,
  `SHIM_CORE_SRCS`, and the `BUILD_PER_APPLET` macro located in the Makefile;
  the `-I` ordering requirement (shim before vendor) is satisfiable at each.
