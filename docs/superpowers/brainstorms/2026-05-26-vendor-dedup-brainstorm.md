# Vendor de-duplication for lorenz and quant: brainstorm

Date: 2026-05-26
Tracks: GitHub issue #20
Kickoff: `docs/superpowers/prompts/2026-05-26-vendor-dedup-kickoff.md`
Origin: deferred item D6 from `docs/superpowers/brainstorms/2026-05-23-cleanup-brainstorm.md`

## Vendor pin (audited)

`vendor/O_C-Phazerville` pinned and checked out at SHA `7800d929f25868f9a8b7d3d50514532ee001649b`.
Submodule was provisioned in this worktree with
`git submodule update --init --depth=1 vendor/O_C-Phazerville` and the
checked-out SHA was verified against the gitlink before any diff was run.
Vendor source root for this work: `vendor/O_C-Phazerville/software/src`
(referenced in the Makefile as `HEM_SRC_DIR`).

## Goal

Remove verbatim copies of vendor lorenz and quant source from `shim/` where
the copy is byte-identical to vendor, and build the vendor source directly
instead. Behavior must not change: every affected build artifact's `.text` is
byte-identical before and after. This is polish, not a defect. The tree builds
and ships today.

## Scope decision

Ship Units 1 and 2. Defer Unit 3 to its own GitHub issue. This is Path 2 from
the issue. The decision is driven by the Unit 3 divergence audit below, which
found a real defensive fix in `braids_quantizer.cpp` and substantive dead-code
and dependency elimination in `OC_scales.cpp`. Neither file is adoptable
verbatim, so the clean-win contract (byte-identical `.text`) does not hold for
Unit 3.

## Re-confirmed diff results (against the pinned SHA)

`diff -q` was re-run at audit time against the pinned vendor SHA. Results:

| File | Vendor counterpart | Result |
| --- | --- | --- |
| `shim/include/lorenz/streams_lorenz_generator.h` | `software/src/streams_lorenz_generator.h` | identical |
| `shim/include/lorenz/streams_resources.h` | `software/src/streams_resources.h` | identical |
| `shim/src/lorenz/streams_lorenz_generator.cpp` | `software/src/streams_lorenz_generator.cpp` | identical |
| `shim/src/lorenz/streams_resources.cpp` | `software/src/streams_resources.cpp` | identical |
| `shim/include/quant/braids_quantizer.h` | `software/src/braids_quantizer.h` | identical |
| `shim/include/quant/braids_quantizer_scales.h` | `software/src/braids_quantizer_scales.h` | identical |
| `shim/include/quant/OC_scales.h` | `software/src/OC_scales.h` | identical |
| `shim/src/quant/braids_quantizer.cpp` | `software/src/braids_quantizer.cpp` | DIFFERS |
| `shim/src/quant/OC_scales.cpp` | `software/src/OC_scales.cpp` | DIFFERS |

The two lorenz bridge headers `shim/src/lorenz/streams_lorenz_generator.h`
(420 B) and `shim/src/lorenz/streams_resources.h` (406 B) are shim-invented
forwarders, not vendor copies. Each is a one-line `#include` redirect that
exists only because the vendor `.cpp` files use bare-name includes and the
`build/arm/shim_src/%.o` rule does not put the vendor source root on the
include path. They are removed with Unit 1.

Shim-invented files that are NOT duplicates and are KEPT:

- `shim/include/quant/MIDIQuantizer.h` (shim-invented)
- `shim/src/quant/q_engine.cpp` (shim-invented)

## Categorization

### Unit 1: lorenz (clean win, lowest risk) — SHIP

All four files byte-identical. The two cpps compile as standalone objects
(`build/arm/shim_src/lorenz/streams_*.o`, Makefile:209), not folded into the
LowerRenz applet translation unit. LowerRenz is the only consumer; it links
the objects via `VENDOR_DEPS_LowerRenz` (Makefile:291). The host build lists
the cpps in `VENDOR_DEP_HOST_SRCS` (Makefile:354).

Approach: delete the four copies and both bridges, build the vendor
`streams_*.cpp` directly, and add `-I$(HEM_SRC_DIR)` to the two object compile
rules so the vendor cpps' bare-name `#include "streams_*.h"` resolve against
vendor headers. Because the objects are separate translation units, the
include-path addition is isolated to those two compiles and does not touch the
LowerRenz applet TU or any other build.

### Unit 2: quant headers (clean win, broader blast radius than the issue implied) — SHIP

All three headers byte-identical. The blast radius is wider than a handful of
consumers because the headers reach the build through `HSUtils.h`, which nearly
every applet TU includes.

Full consumer enumeration (header-path includes of the duplicated quant
headers; `quant/MIDIQuantizer.h` is shim-invented and excluded):

| Site | Include |
| --- | --- |
| `shim/include/HSUtils.h:12` | `#include "quant/OC_scales.h"` |
| `shim/src/quant/braids_quantizer.cpp:5` | `#include "quant/braids_quantizer.h"` (kept Unit 3 cpp) |
| `shim/src/quant/q_engine.cpp:4` | `#include "quant/OC_scales.h"` (kept shim cpp) |
| `shim/src/quant/OC_scales.cpp:5` | `#include "quant/OC_scales.h"` (kept Unit 3 cpp) |
| `harness/tests/test_dep_quant.cpp:13` | `#include "quant/OC_scales.h"` |

Internal bare-name includes inside the duplicated headers (resolve in the
vendor source directory once the build points there):

| Site | Include |
| --- | --- |
| `shim/include/quant/braids_quantizer_scales.h:32` | `#include "braids_quantizer.h"` |
| `shim/include/quant/OC_scales.h:6` | `#include "braids_quantizer.h"` |
| `shim/include/quant/OC_scales.h:7` | `#include "braids_quantizer_scales.h"` |

Vendor places these three files bare in `software/src/` (no `quant/` subdir),
so the repoint cannot keep the `quant/` include prefix. It requires:

- bare-name includes (`OC_scales.h`, not `quant/OC_scales.h`) at the five
  consumer sites above
- `-I$(HEM_SRC_DIR)` added to the shared include flag, appended AFTER
  `-Ishim/include` so shim stubs continue to shadow any same-named vendor
  header (`OC_core.h`, `Arduino.h`, `util/util_misc.h`, and similar)

Shadowing risk and why it is contained:

- `-I` order. With `-Ishim/include` first and `-I$(HEM_SRC_DIR)` last, gcc
  takes the shim copy for any colliding basename. Everything compiles today,
  so every bare include already resolves via some first match; appending
  vendor source last cannot displace an existing first match. The only new
  resolutions are the three quant headers we delete on purpose.
- Backstop. The per-`.o` byte-identical `.text` gate detects any unexpected
  shadow: a shadowed header changes that object's `.text` hash and the work
  halts. The risk is detected, not silent.

Coupling to Unit 3: deferring Unit 3 keeps the shim quant cpps, which include
`quant/braids_quantizer.h` and `quant/OC_scales.h`. Unit 2 must repoint those
kept cpp include lines to bare names as well. This is behavior-preserving
because the vendor header content is byte-identical to the deleted shim header.
Unit 2 and Unit 3 touch only at the include line, not at any logic.

### Unit 3: quant cpps (real divergence) — DEFER to its own issue

`shim/src/quant/braids_quantizer.cpp` and `shim/src/quant/OC_scales.cpp` are
both genuinely divergent from vendor. Two independent disqualifiers, either of
which alone defers the unit.

Disqualifier A, `braids_quantizer.cpp` field-init is a real defensive fix.

Evidence:

- Shim `Quantizer::Init()` initializes four members that vendor `Init()` omits:
  `num_notes_ = 0; span_ = 0; note_number_ = 0; requantize_ = false;`.
- Vendor `Init()` initializes only `enabled_`, `codeword_`, `transpose_`,
  `previous_boundary_`, `next_boundary_`, `octave_constraint_`,
  `octave_constraint_min_`, `octave_constraint_max_`.
- Vendor constructor is `Quantizer() {}` (empty, no member initializers).
- The header declares `span_`, `num_notes_`, `note_number_`, `requantize_`
  with no default member initializers
  (`shim/include/quant/braids_quantizer.h:108,110,115,116`).
- Vendor `Process()` reads `requantize_` before any code path sets it:
  `if (!requantize_ && pitch >= previous_boundary_ ...)`. `Requantize()` only
  ever sets `requantize_` to true; nothing in vendor sets it to false.

Verdict: fix, not convention. After construction plus `Init()`, vendor leaves
`requantize_` and `note_number_` indeterminate, and `Process()` reads
`requantize_`. The shim's explicit initialization is correct. Adopting the
vendor cpp verbatim would regress it. This is the kickoff stop condition
"Unit 3 divergence audit finds the field-init change is a real
vendor-bug-fix."

Disqualifier B, `OC_scales.cpp` strips real dependencies and dead code.

The shim cpp differs from vendor by more than includes:

- removes `SaveToScala` and `LoadScala` (SD-card dead code; no SD card on NT)
- replaces the full `scale_names`, `scale_names_short`, and `voltage_scalings`
  string tables with `nullptr` stubs (no display subsystem reads them)
- drops `#include "avr/pgmspace.h"` and the `FLASHMEM` markers (AVR-only)
- drops two `static_assert`s on `ARRAY_SIZE(scale_names)` versus
  `braids::scales`

Adopting the vendor cpp verbatim would pull Arduino and SD-card dependencies
(`avr/pgmspace.h`, the `File` type) and regrow the artifact with multi-hundred
line string tables that nothing on the NT reads.

Path 1 from the issue (shim stubs for `OC_options.h` and `util/util_misc.h`,
then build vendor cpps verbatim) does not clear the audit. Defer Unit 3 and
file a follow-up issue describing both disqualifiers.

## Win quantification

Removing the four lorenz copies, the two lorenz bridges, and the three quant
headers eliminates roughly 2474 duplicated lines (measured by `wc -l` across
the nine files) plus the two forwarding bridges and the include indirection
they forced.

## Out of scope

- `vendor/` (read-only submodules; never edit)
- `build/`
- any change that alters behavior or any artifact's `.text`
- upstreaming the cleanup to vendor (issue Path 3)
- a broader `shim/include/` versus `shim/src/` reorganization beyond removing
  the bridges these copies forced
- Unit 3 quant cpps (deferred to a follow-up issue)

## Verification contract (carried into the spec)

The correctness contract is byte-identical `.text`, not merely a clean build.

- Baseline: for each affected `.o`, extract and hash the `.text` section before
  any change, for example
  `arm-none-eabi-objcopy -O binary --only-section=.text build/arm/LowerRenz.o
  /tmp/lowerrenz.text && shasum -a 256 /tmp/lowerrenz.text`.
- Per commit (one unit per commit): the affected `.o` rebuilds and its `.text`
  hash matches baseline. A hash change means the copy and the vendor source
  were not identical at build time; halt and investigate.
- Aggregate: `make test-applets`, `make arm`, and `make test` all pass.

## Stop conditions (carried from kickoff)

- a `.text` hash delta on any affected `.o` the de-dup was meant to leave
  untouched: halt and investigate
- a `diff -q` at audit time that no longer reports identical (already
  re-confirmed identical for Units 1 and 2 at SHA `7800d929`)
- the work turns up a genuine bug rather than a duplication: halt and escalate

## Status

| Unit | Files | Status |
| --- | --- | --- |
| 1 lorenz | 4 copies + 2 bridges | ship |
| 2 quant headers | 3 headers | ship |
| 3 quant cpps | 2 cpps | defer to follow-up issue |
