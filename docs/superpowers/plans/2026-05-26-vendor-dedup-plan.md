# Vendor de-duplication for lorenz and quant: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace verbatim shim copies of vendor lorenz and quant source with
the vendor originals built in place, leaving every ARM artifact's `.text`
byte-identical.

**Architecture:** Point the build at `vendor/O_C-Phazerville/software/src`
(`HEM_SRC_DIR`) and delete the copies plus the forwarding bridges they forced.
Correctness is gated on a per-object `.text` hash that must not move. Unit 1
(lorenz) carries a shared `SHIM_INCLUDE` include-path edit that Unit 2 (quant
headers) relies on, so the two units are sequenced, not parallel. Unit 3 (quant
cpps) is deferred to a follow-up issue per the divergence audit.

**Tech Stack:** GNU Make, `arm-none-eabi-c++`/`-gcc`, `arm-none-eabi-objcopy`,
Catch2 host harness, `shasum`.

---

## Why this is not parallel subagent fan-out

The spec's shared `SHIM_INCLUDE` edit is touched by both units, and the total
work is two small commits. The parallel-execution rule applies when items are
file-disjoint and each is minutes of isolated work; here the shared Makefile
line and the tiny size make a single sequential session correct. No worktree
fan-out.

## File map

Deleted by Unit 1 (lorenz):

- `shim/include/lorenz/streams_lorenz_generator.h`
- `shim/include/lorenz/streams_resources.h`
- `shim/src/lorenz/streams_lorenz_generator.cpp`
- `shim/src/lorenz/streams_resources.cpp`
- `shim/src/lorenz/streams_lorenz_generator.h` (bridge)
- `shim/src/lorenz/streams_resources.h` (bridge)

Deleted by Unit 2 (quant headers):

- `shim/include/quant/braids_quantizer.h`
- `shim/include/quant/braids_quantizer_scales.h`
- `shim/include/quant/OC_scales.h`

Modified:

- `Makefile` (Unit 1: `SHIM_INCLUDE`, new `vendor_src` rule,
  `VENDOR_DEPS_LowerRenz`, `VENDOR_DEP_HOST_SRCS`)
- `harness/tests/test_dep_lorenz.cpp` (Unit 1: include + comment)
- `shim/include/HSUtils.h` (Unit 2: include + comment)
- `shim/src/quant/braids_quantizer.cpp` (Unit 2: include line)
- `shim/src/quant/q_engine.cpp` (Unit 2: include line)
- `shim/src/quant/OC_scales.cpp` (Unit 2: include line)
- `harness/tests/test_dep_quant.cpp` (Unit 2: include line)

Unchanged and kept (shim-invented, not duplicates):

- `shim/include/quant/MIDIQuantizer.h`, `shim/src/quant/q_engine.cpp` logic,
  `shim/src/quant/braids_quantizer.cpp` logic, `shim/src/quant/OC_scales.cpp`
  logic.

---

## Task 0: Provision and capture the baseline

**Files:** none (read-only baseline).

- [ ] **Step 1: Provision toolchain and submodules**

Run: `./bootstrap.sh`
Expected: exits 0; all three submodules present
(`vendor/O_C-Phazerville` at `7800d929`, `vendor/distingNT_API`,
`vendor/llvm-project` sparse).

- [ ] **Step 2: Confirm the vendor pin**

Run: `git -C vendor/O_C-Phazerville rev-parse HEAD`
Expected: `7800d929f25868f9a8b7d3d50514532ee001649b`

- [ ] **Step 3: Build all ARM objects**

Run: `make arm`
Expected: exits 0; `build/arm/*.o` populated (55 applets plus hosts and
probes).

- [ ] **Step 4: Hash every object's `.text` as the baseline**

```bash
hash_text() {
  for o in build/arm/*.o; do
    arm-none-eabi-objcopy -O binary --only-section=.text "$o" /tmp/t.text
    printf '%s  %s\n' "$(shasum -a 256 /tmp/t.text | cut -d' ' -f1)" "$(basename "$o")"
  done | sort -k2
}
hash_text > /tmp/text-baseline.txt
wc -l /tmp/text-baseline.txt
```

Expected: one line per `build/arm/*.o`. Keep `/tmp/text-baseline.txt` for the
rest of the plan. Re-run `hash_text` after each unit and `diff` against it.

---

## Task 1: Unit 1 lorenz (carries the shared `SHIM_INCLUDE` enabler)

**Files:**
- Modify: `Makefile` (`SHIM_INCLUDE` line 156; new rule near line 209;
  `VENDOR_DEPS_LowerRenz` line 291; `VENDOR_DEP_HOST_SRCS` line 354)
- Modify: `harness/tests/test_dep_lorenz.cpp:17` and its comment block
- Delete: the six lorenz files in the file map

- [ ] **Step 1: Add the vendor source root to the shared include flag**

In `Makefile`, change line 156 from:

```make
SHIM_INCLUDE := -Ishim/include
```

to:

```make
SHIM_INCLUDE := -Ishim/include -I$(HEM_SRC_DIR)
```

`-Ishim/include` stays first so shim stubs shadow same-named vendor headers.

- [ ] **Step 2: Prove the include-path edit alone is `.text`-neutral**

Run:

```bash
make arm && \
for o in build/arm/*.o; do
  arm-none-eabi-objcopy -O binary --only-section=.text "$o" /tmp/t.text
  printf '%s  %s\n' "$(shasum -a 256 /tmp/t.text | cut -d' ' -f1)" "$(basename "$o")"
done | sort -k2 > /tmp/text-step.txt
diff /tmp/text-baseline.txt /tmp/text-step.txt && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL`. A non-empty diff means a vendor header shadowed a
shim stub. Halt and investigate the offending object before continuing.

- [ ] **Step 3: Add an ARM rule that compiles vendor cpps in place**

In `Makefile`, immediately after the existing `build/arm/shim_src/%.o` rule
(ends at line 209), add:

```make
# Pattern rule for vendor dep .cpp implementations compiled in place from
# the vendor source tree. The vendor cpps include their headers by bare name;
# compiling in place lets the compiler find the sibling header via its
# own-directory search, so no forwarding bridge is needed.
build/arm/vendor_src/%.o: $(HEM_SRC_DIR)/%.cpp
	mkdir -p $(@D)
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) -c -o $@ $<
```

- [ ] **Step 4: Repoint LowerRenz ARM dep objects to the vendor rule**

In `Makefile`, change line 291 from:

```make
VENDOR_DEPS_LowerRenz          := build/arm/shim_src/lorenz/streams_resources.o build/arm/shim_src/lorenz/streams_lorenz_generator.o
```

to:

```make
VENDOR_DEPS_LowerRenz          := build/arm/vendor_src/streams_resources.o build/arm/vendor_src/streams_lorenz_generator.o
```

- [ ] **Step 5: Repoint the host dep sources to vendor**

In `Makefile`, change lines 354-355 from:

```make
VENDOR_DEP_HOST_SRCS := shim/src/lorenz/streams_resources.cpp \
                       shim/src/lorenz/streams_lorenz_generator.cpp
```

to:

```make
VENDOR_DEP_HOST_SRCS := $(HEM_SRC_DIR)/streams_resources.cpp \
                       $(HEM_SRC_DIR)/streams_lorenz_generator.cpp
```

- [ ] **Step 6: Repoint the dep test include to a bare name**

In `harness/tests/test_dep_lorenz.cpp`, change line 17 from:

```cpp
#include "lorenz/streams_lorenz_generator.h"
```

to:

```cpp
#include "streams_lorenz_generator.h"
```

Then rewrite the comment block at lines 5-8 so it no longer describes the
deleted forwarding bridges. Replace it with:

```cpp
// The vendor lorenz sources are compiled in place from
// vendor/O_C-Phazerville/software/src. This test includes the vendor header
// by bare name; -I$(HEM_SRC_DIR) on SHIM_INCLUDE resolves it.
```

- [ ] **Step 7: Delete the six lorenz files**

```bash
git rm shim/include/lorenz/streams_lorenz_generator.h \
       shim/include/lorenz/streams_resources.h \
       shim/src/lorenz/streams_lorenz_generator.cpp \
       shim/src/lorenz/streams_resources.cpp \
       shim/src/lorenz/streams_lorenz_generator.h \
       shim/src/lorenz/streams_resources.h
```

Expected: six files staged for deletion; `shim/include/lorenz/` and
`shim/src/lorenz/` are now empty (let git drop them).

- [ ] **Step 8: Rebuild and gate on byte-identical `.text`**

Run:

```bash
make arm && \
for o in build/arm/*.o; do
  arm-none-eabi-objcopy -O binary --only-section=.text "$o" /tmp/t.text
  printf '%s  %s\n' "$(shasum -a 256 /tmp/t.text | cut -d' ' -f1)" "$(basename "$o")"
done | sort -k2 > /tmp/text-after.txt
diff /tmp/text-baseline.txt /tmp/text-after.txt && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL`. If `build/arm/LowerRenz.o` (or any object) differs,
the copy and vendor source were not identical at build time. Halt; do not
accept the artifact.

- [ ] **Step 9: Run host coverage for the relocated source**

Run: `make test-deps && make test-applets`
Expected: both exit 0; `test_dep_lorenz` and `test_applet_LowerRenz` pass.

- [ ] **Step 10: Optional dead-rule cleanup**

Check whether anything still uses the old `shim_src` rule:

Run: `grep -n 'build/arm/shim_src' Makefile`
Expected: no matches. If none, remove the now-unused `build/arm/shim_src/%.o`
rule (the three lines at 207-209 plus its comment at 203-206). If there are
matches, leave the rule in place.

- [ ] **Step 11: Commit**

```bash
git add Makefile harness/tests/test_dep_lorenz.cpp
git commit -m "refactor(shim): build vendor lorenz source directly, drop copies" \
           -m "Refs: #20"
```

---

## Task 2: Unit 2 quant headers

**Files:**
- Modify: `shim/include/HSUtils.h:12` and comment at line 6
- Modify: `shim/src/quant/braids_quantizer.cpp:5`
- Modify: `shim/src/quant/q_engine.cpp:4`
- Modify: `shim/src/quant/OC_scales.cpp:5`
- Modify: `harness/tests/test_dep_quant.cpp:13`
- Delete: the three quant headers in the file map

- [ ] **Step 1: Repoint the HSUtils include, preserve the pragma wrapper**

In `shim/include/HSUtils.h`, change line 12 from:

```cpp
#include "quant/OC_scales.h"
```

to:

```cpp
#include "OC_scales.h"
```

Leave the surrounding `#pragma GCC diagnostic push` / `ignored "-Waddress"` /
`pop` lines (10-13) intact. Change the comment at line 6 from
`pulled in via quant/OC_scales.h` to `pulled in via OC_scales.h`.

- [ ] **Step 2: Repoint the kept quant cpp includes**

In `shim/src/quant/braids_quantizer.cpp` line 5, change
`#include "quant/braids_quantizer.h"` to `#include "braids_quantizer.h"`.

In `shim/src/quant/q_engine.cpp` line 4, change
`#include "quant/OC_scales.h"` to `#include "OC_scales.h"`.

In `shim/src/quant/OC_scales.cpp` line 5, change
`#include "quant/OC_scales.h"` to `#include "OC_scales.h"`.

- [ ] **Step 3: Repoint the quant dep test include**

In `harness/tests/test_dep_quant.cpp` line 13, change
`#include "quant/OC_scales.h"` to `#include "OC_scales.h"`. Leave line 14
(`#include "quant/MIDIQuantizer.h"`) unchanged.

- [ ] **Step 4: Delete the three duplicated quant headers**

```bash
git rm shim/include/quant/braids_quantizer.h \
       shim/include/quant/braids_quantizer_scales.h \
       shim/include/quant/OC_scales.h
```

Expected: three files staged for deletion; `shim/include/quant/MIDIQuantizer.h`
remains.

- [ ] **Step 5: Rebuild and gate on byte-identical `.text`**

Run:

```bash
make arm && \
for o in build/arm/*.o; do
  arm-none-eabi-objcopy -O binary --only-section=.text "$o" /tmp/t.text
  printf '%s  %s\n' "$(shasum -a 256 /tmp/t.text | cut -d' ' -f1)" "$(basename "$o")"
done | sort -k2 > /tmp/text-after.txt
diff /tmp/text-baseline.txt /tmp/text-after.txt && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL` across the full set (every applet includes HSUtils).
Any delta means a quant header was not byte-identical to vendor at build time.
Halt and investigate.

- [ ] **Step 6: Run quant coverage**

Run: `make test-deps && make test-applets`
Expected: both exit 0; `test_dep_quant` and the quant-using applet tests
(EnigmaJr, MultiScale, Calibr8, and the rest) pass.

- [ ] **Step 7: Commit**

```bash
git add shim/include/HSUtils.h shim/src/quant/braids_quantizer.cpp \
        shim/src/quant/q_engine.cpp shim/src/quant/OC_scales.cpp \
        harness/tests/test_dep_quant.cpp
git commit -m "refactor(shim): drop duplicated quant headers, include vendor directly" \
           -m "Refs: #20"
```

---

## Task 3: Aggregate verification and Unit 3 follow-up

**Files:** none (verification plus issue filing).

- [ ] **Step 1: Full test suite**

Run: `make test`
Expected: exits 0 (host build, applet tests, scripted scenario all pass).

- [ ] **Step 2: Final `.text` parity confirmation**

Run the `hash_text` diff from Task 0 Step 4 one more time against
`/tmp/text-baseline.txt`.
Expected: `TEXT IDENTICAL`. This is the end-to-end proof that two commits of
de-duplication moved zero bytes of deployable code.

- [ ] **Step 3: Confirm the duplication is gone**

Run:

```bash
git -C vendor/O_C-Phazerville rev-parse HEAD
diff -q shim/src 2>/dev/null; ls shim/include/lorenz 2>&1; ls shim/include/quant
```

Expected: `shim/include/lorenz` absent; `shim/include/quant` contains only
`MIDIQuantizer.h`.

- [ ] **Step 4: File the Unit 3 follow-up issue**

Run:

```bash
gh issue create --repo danryan/nt_and_crime \
  --title "Vendor de-dup Unit 3: quant cpps (deferred from #20)" \
  --body "$(cat <<'EOF'
Deferred from #20. The two quant cpps are NOT byte-identical to vendor and are
not adoptable verbatim. Two independent disqualifiers (see
docs/superpowers/brainstorms/2026-05-26-vendor-dedup-brainstorm.md):

1. shim/src/quant/braids_quantizer.cpp adds defensive field init in Init()
   (num_notes_/span_/note_number_/requantize_ = 0/false) that vendor omits and
   Process() reads. Adopting vendor verbatim regresses a real
   uninitialized-member fix.
2. shim/src/quant/OC_scales.cpp strips SaveToScala/LoadScala (SD-card dead
   code), the full scale-name string tables (nullptr stubs), avr/pgmspace.h,
   FLASHMEM, and two static_asserts. Vendor verbatim would pull Arduino/SD deps
   and regrow the artifact.

Path forward if revisited: classify each divergence, keep the field-init fix,
and decide whether the OC_scales dead-code strip is worth preserving as a shim
delta rather than de-duplicated.
EOF
)"
```

Expected: issue created; capture its number for the PR description.

- [ ] **Step 5: Open the PR**

Detect the default branch, then open the PR with both commits:

```bash
git remote show origin | grep 'HEAD branch' | awk '{print $NF}'
```

Use the result as `--base`. PR summary covers: what (drop lorenz copies and
quant header copies, build vendor directly), why (each copy is a silent
divergence point; removes the bridge indirection), how (byte-identical `.text`
gate). Test plan checkboxes: `make arm` TEXT IDENTICAL vs baseline,
`make test` passes, Unit 3 deferred to the new issue. Footer `Refs: #20`.

---

## Self-review

Spec coverage: the canonical recipe maps to Task 0 (baseline) plus the
rebuild-and-diff steps in Tasks 1, 2, 3. Unit 1 maps to Task 1; Unit 2 to
Task 2; Unit 3 defer to Task 3 Step 4. The shared `SHIM_INCLUDE` enabler is
Task 1 Step 1 with its standalone `.text`-neutral checkpoint at Step 2. The
`-Waddress` pragma preservation is Task 2 Step 1. The `MIDIQuantizer.h` keep is
Task 2 Steps 3 and 4.

Placeholder scan: no TBD/TODO; every edit shows exact before/after; every gate
shows the command and expected output.

Consistency: `HEM_SRC_DIR`, `SHIM_INCLUDE`, `VENDOR_DEPS_LowerRenz`,
`VENDOR_DEP_HOST_SRCS`, and `build/arm/vendor_src/%.o` are used identically
across tasks. The `hash_text` gate command is identical in Tasks 0, 1, 2, 3.
