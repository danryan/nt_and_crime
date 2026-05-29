# BYTEBEATGEN OC::App Port Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: use superpowers:test-driven-development
> to implement task-by-task. Steps use checkbox syntax for tracking.

Goal: port `APP_BYTEBEATGEN.h` to one NT plug-in `.o` under `plugins/apps/`.

Architecture: single-TU per-app port on the O_C apps foundation, reusing the BBGEN
quad facade. Layer 0 raises the shared `kMaxSettings` cap and adds two shim symbols;
the per-app port follows.

Tech stack: C++20, `arm-none-eabi-c++` (NT target) + `clang++` (host), Catch2.

Branch: `dr/oc-app-bytebeatgen` off `main`. Single-app sequential port, one PR. No
parallel fan-out (the track is one app at a time; Layer 0 and the port are
sequential here).

---

## Layer 0: shared shim + runtime prerequisites

### Task 1: Raise kMaxSettings to 80

Files:

- Modify: `plugins/apps/_per_app_runtime.h` (the `kMaxSettings` line, ~90)

- [ ] Step 1: edit `constexpr int kMaxSettings = 64;` to `= 80;`. Leave a comment
      noting BYTEBEATGEN's 4*19=76 flat settings drove the raise.
- [ ] Step 2: `make test-oc-app-BBGEN test-oc-app-FPART` - existing quad/large-settings
      apps still green (the raise only enlarges static arrays).
- [ ] Step 3: commit `feat(oc-apps): raise per-app kMaxSettings 64 -> 80 for BYTEBEATGEN`.

### Task 2: Add bytebeat_equation_names string table

Files:

- Modify: `shim/include/OC_strings.h` (extern decl, in `namespace OC::Strings`)
- Modify: `shim/src/globals.cpp` (definition, near `trigger_input_names`)

- [ ] Step 1: in `OC_strings.h`, after the BBGEN string externs, add
      `extern const char* const bytebeat_equation_names[];` with a comment that it
      labels the BYTEBEATGEN Equation setting (vendor `OC_strings.cpp:127`).
- [ ] Step 2: in `globals.cpp`, define
      `const char* const bytebeat_equation_names[] = { "hope", "love", "life", "age", "clysm", "monk", "NERV", "Trurl", "Pirx", "Snaut", "Hari", "Kris", "Tichy", "Bregg", "Avon", "Orac" };`
      (mirror vendor byte-for-byte).
- [ ] Step 3: `make test-buses` (cheap host build that links globals.cpp) - compiles.
- [ ] Step 4: commit `feat(shim): add bytebeat_equation_names for BYTEBEATGEN`.

### Task 3: Add util_misc.h shim (reverse_byte)

Files:

- Create: `shim/include/util/util_misc.h`

- [ ] Step 1: create the header with `#pragma once`, `#include <cstdint>`, and the
      single inline mirroring vendor `util/util_misc.h:31`:

```cpp
#pragma once
#include <cstdint>
// Minimal shadow of vendor util/util_misc.h: the BYTEBEATGEN screensaver calls
// util::reverse_byte. The vendor header drags OC_options.h and unrelated structs,
// so the shim provides only the one inline.
namespace util {
inline uint8_t reverse_byte(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}
}  // namespace util
```

- [ ] Step 2: verify the bit-twiddle against vendor (read
      `vendor/O_C-Phazerville/software/src/util/util_misc.h:31-36`, match exactly).
- [ ] Step 3: commit `feat(shim): add util_misc.h reverse_byte shadow`.

---

## Layer 1: the BYTEBEATGEN port (TDD)

### Task 4: Manifest header

Files:

- Create: `shim/include/oc_app_manifests/BYTEBEATGEN.h`

- [ ] Step 1: copy `BBGEN.h`, change guid to `NT_MULTICHAR('O','C','B','T')`, name
      "Byte Beats", description "Four bytebeat generators (O_C APP_BYTEBEATGEN port)",
      and the bus row comments (CV in 1..4 smoothed ADC; CV out A..D the four
      bytebeat samples; TR in 1..4 the per-channel trigger select). Inputs/outputs/
      triggers stay the 4/4/4 generic block.
- [ ] Step 2: commit `feat(oc-apps): add BYTEBEATGEN manifest (guid OCBT)`.

### Task 5: Failing host test

Files:

- Create: `harness/tests/test_oc_app_BYTEBEATGEN.cpp`
- Modify: `Makefile` (`OC_APP_LIST += BYTEBEATGEN`, `VENDOR_DEPS_BYTEBEATGEN`,
  `VENDOR_DEP_HOST_SRCS_BYTEBEATGEN`)

- [ ] Step 1: add to Makefile after the BBGEN lines:
      `OC_APP_LIST := StubApp Low_rents Harrington1200 FPART BBGEN BYTEBEATGEN`,
      `VENDOR_DEPS_BYTEBEATGEN := build/arm/vendor_src/peaks_bytebeat.o`,
      `VENDOR_DEP_HOST_SRCS_BYTEBEATGEN := $(HEM_SRC_DIR)/peaks_bytebeat.cpp`.
- [ ] Step 2: write the Catch2 test using the seams the port will export
      (`bytebeatgen_get_setting`, `bytebeatgen_apply_setting`, `bytebeatgen_setting_count`,
      `bytebeatgen_settings_per_channel`, `bytebeatgen_settings_param_base`,
      `bytebeatgen_param_name`, `bytebeatgen_arm_sentinel`, plus the loader path for
      numParameters/name assertions). Cases below.
- [ ] Step 3: `make build/host/test_oc_app_BYTEBEATGEN` - FAILS to link (no port yet).
      Expected: undefined `bytebeatgen_*` seams.

Test cases (`[bytebeatgen]`):

- `setting_count` - `bytebeatgen_setting_count() == 76`,
  `bytebeatgen_settings_per_channel() == 19`.
- `round_trip` - for each of 76 flat rows: apply a representative in-range value,
  read it back equal; assert per-channel isolation (writing channel A row leaves
  channel B's same setting unchanged).
- `param_table` - load via plugin loader; `numParameters == 88`;
  `parameters[base].name == "A Equation"`; `parameters[base+75].name == "D CV4 -> "`;
  name buffer never overruns 16 bytes.
- `full_scale_output` - route channel A out to a bus, drive a trigger, step; assert
  the bus value lands in 0V..+5V code space and an unrouted channel's bus stays 0V.
- `conditional_menu` - via seam, set channel A LOOP_MODE=0 -> enabled count 13; set
  LOOP_MODE=1 -> enabled count 19 (exercises `update_enabled_settings`).

### Task 6: Implement the port to green

Files:

- Create: `plugins/apps/BYTEBEATGEN.cpp`

- [ ] Step 1: copy `plugins/apps/BBGEN.cpp` verbatim as the starting point.
- [ ] Step 2: rename: `BBGEN` -> `BYTEBEATGEN` thunks, `QuadBouncingBalls` ->
      `QuadByteBeats`, `bbgen` -> `bytebeatgen`, `BouncingBall` -> `ByteBeat`,
      `BB_SETTING_LAST` -> `BYTEBEAT_SETTING_LAST`, manifest include -> BYTEBEATGEN.h,
      `ManifestNS = oc_app::BYTEBEATGEN`, GUID comment OCBT, app var
      `the_bytebeatgen_app`, instance `BYTEBEATGENInstance`, seams `bytebeatgen_*`.
      `kNumSettings = 4 * 19 = 76`, `numParameters = kIoParamCount + 76`.
- [ ] Step 3: add `#include "util/util_misc.h"` and ensure `#include "util/util_history.h"`
      reachable (vendor header pulls it; add to the includes if the first compile needs it).
- [ ] Step 4: add a `bytebeatgen_enabled_count(int channel)` seam returning
      `bytebeatgen.bytebeats_[channel].num_enabled_settings()` after
      `update_enabled_settings()`, for the conditional_menu test.
- [ ] Step 5: `make build/host/test_oc_app_BYTEBEATGEN && ./build/host/test_oc_app_BYTEBEATGEN`
      - all cases PASS. Fix shim gaps surfaced at first compile (expected additive work).
- [ ] Step 6: commit `feat(apps): port BYTEBEATGEN (APP_BYTEBEATGEN) O_C app to NT plug-in`.

### Task 7: ARM build + verification

- [ ] Step 1: `make build/arm/BYTEBEATGEN.o`.
- [ ] Step 2: `arm-none-eabi-readelf -W -S build/arm/BYTEBEATGEN.o | grep '\.text'`
      - `.text` 16-20 KB, under 82 KB.
- [ ] Step 3: `arm-none-eabi-nm build/arm/BYTEBEATGEN.o | grep ' U '` - only the
      firmware-contract symbols unresolved; `peaks::ByteBeat` resolved.
- [ ] Step 4: `make test-oc-apps test-applets` - full regression green.
- [ ] Step 5: commit any doc/Makefile cleanup; update `docs/shim-additions.md` and
      `CLAUDE.md` (BYTEBEATGEN learnings: kMaxSettings raise, util_misc, equation names).
- [ ] Step 6: `markdownlint` the three docs.

---

## Self-review

- Spec coverage: kMaxSettings raise (T1), equation names (T2), util_misc (T3),
  manifest (T4), tests (T5), port (T6), ARM verify (T7). All spec sections mapped.
- Type consistency: `kNumSettings=76`, `kSettingsPerChannel=19`,
  `numParameters=88`, GUID OCBT used consistently across tasks.
- No placeholders: all code shown or copied from the named BBGEN template.
