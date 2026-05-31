# Design: ASR (Analog Shift Register) NT plug-in

Date: 2026-05-31
Status: implemented
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #49

Implements the audit in `../brainstorms/2026-05-31-ASR-brainstorm.md`. The
PASSENCORE single-instance template, plus the integer-sequence digit-table
subsystem ported into the shim.

## Recipe

`plugins/apps/ASR.cpp` follows the PASSENCORE single-instance template:

1. `#define NT_OC_APP_TU 1`; `#include "UI/ui_events.h"` FIRST (OC::UI alias for
   the vendor `OC::ScaleEditor<ASRApp>` member of `asr_state`).
2. Include the runtime, dispatch, the OC shim headers, AND
   `#include "util/util_math.h"` BEFORE `APP_ASR.h` (ASR uses `SlewedValue` /
   `USAT16` from the shim util_math.h shadow; the host force-includes it but the
   ARM build does not, so the explicit include is load-bearing on ARM), plus
   `util/util_turing.h`.
3. `namespace menu = OC::menu;`. `uint_fast8_t MENU_REDRAW = 1;`.
4. `#define ENABLE_APP_ASR 1`; `#include "APP_ASR.h"`.
5. Subset: exclude the U16 `ASR_SETTING_MASK` (index 3). `phys_setting(w) =
   w >= 3 ? w + 1 : w`, `num_settings = ASR_SETTING_LAST - 1 = 26`. Build
   `make_facade(&asr)`, override get/apply/value_attr with the skip remap; keep
   the default save/restore/storage_size (single SettingsBase covers the mask).
6. `.customUi = dispatch_custom_ui_factory<true>` (ASR reads
   `EVENT_BUTTON_LONG_PRESS`).
7. Test seams: `asr_get_setting`/`asr_apply_setting` (logical->phys),
   `asr_setting_count` (26), `asr_settings_param_base`, `asr_get_mask`/
   `asr_set_mask`, `asr_get_outputs`, `asr_arm_sentinel`.

### Manifest

`shim/include/oc_app_manifests/ASR.h`: GUID `OCAS`, name "Analog Shift Reg". 4 CV
in, 4 CV out (the four shift-register stages), 4 trig in (clock).

### Shim additions

- Full `scale_names_short` table (vendor OC_scales.cpp:101) into shim
  OC_scales.cpp (was a nullptr stub; ASR's SCALE uses it).
- `kIntSeqLen = 128` (global) in shim OC_strings.h.
- `OC::Strings::{mult, integer_sequence_names, integer_sequence_dirs}` and the 8
  integer-sequence digit tables (`pi_digits`, `van_eck`,
  `sum_of_squares_of_digits_of_n`, `digsum_of_n`, `digsum_of_n_base4`,
  `digsum_of_n_base5`, `count_down_by_2`, `interspersion_of_A163253`), each
  `uint8_t[128]`, in shim globals.cpp (verbatim from vendor; token-diffed).
- `SlewedValue` + `Atten` (shared `ATTEN_DEFINED` sentinel with CVInputMap.h) in
  shim util/util_math.h.
- `OC::bitmap_hold_indicator_4x8` (OC_bitmaps.h + menus.cpp).
- `randomSeed` in shim Arduino.h guarded by `NT_OC_APP_TU` (the hem path defines
  it in HemisphereApplet.h; the guard keeps the two definitions in disjoint TUs).
- `popcountdi2.c` added to `COMPILER_RT_OBJS` (ASR odr-uses `__popcountdi2`).

### Makefile

`OC_APP_LIST += ASR`; `VENDOR_DEPS_ASR := build/arm/vendor_src/peaks_bytebeat.o`,
`VENDOR_DEP_HOST_SRCS_ASR := $(HEM_SRC_DIR)/peaks_bytebeat.cpp`.

## Test plan (harness/tests/test_oc_app_ASR.cpp)

- Factory/registration: GUID `OCAS`; 26 exposed settings.
- Subset remap: logical 3 maps to `ASR_SETTING_INDEX` (physical 4), not the mask;
  the mask keeps its U16 default.
- Draw renders.
- Output smoke: route clock + CV, pulse the clock, assert the four DAC outputs
  are not all zero (ISR -> quantizer -> shift-register -> DAC runs). Exact pitch
  is a hardware-smoke concern (shim collapses the calibrated DAC to 1V/oct).
- Settings + mask round-trip: mutate all 26 exposed rows plus the mask,
  serialise/deserialise, assert all survive.
- Param sync: direction 1 via `parameterChanged` (OCTAVE); direction 2 via
  `factory->customUi` editing the cursor's setting. NOTE: the scrollable cursor
  list starts at ROOT (SCALE/OCTAVE are left-encoder-steered; the mask row opens
  the editor on BUTTON_R), found by probing `enabled_setting_at`; the test
  toggles editing at the start position (ROOT) and edits it. Offset push-back
  with `set_parameter_offset(1)`.

## Footer

### Recipe spot-check

PASSENCORE single-instance template confirmed reusable; the only deltas are the
mask index (3 not 1) and the shim data additions.

### Per-entry verification (3 settings traced against vendor)

- Index 0 `ASR_SETTING_SCALE`: `{SCALE_SEMI, 0, NUM_SCALES-1, "Scale",
  scale_names_short, U8}`. int16-safe. Exposed (logical 0).
- Index 3 `ASR_SETTING_MASK`: `{65535, 1, 65535, "mask", NULL, U16}`. Overflows
  int16. Excluded.
- Index 4 `ASR_SETTING_INDEX`: `{0, 0, ASR_HOLD_BUF_SIZE-1, "buf.index", NULL,
  U8}`. int16-safe. Exposed at logical 3 (mask skipped).

3 of 3 consistent with vendor source. The 8 digit tables were token-diffed
against vendor OC_strings.cpp (identical, 128 entries each).

### Shim prereq verification

- OC aggregation supplies braids_quantizer + OC_scales; only `peaks_bytebeat.cpp`
  is a per-app vendor dep. `OC_visualfx`/`util_turing`/`util_ringbuffer`/
  `util_integer_sequences`/`dspinst` header-only.
- `util_integer_sequences.h` quote-includes vendor `../OC_strings.h`; the shim
  OC_strings poison handles it because the per-app `.cpp` pulls shim OC_strings.h
  before APP_ASR.h.
- GUID `OCAS` unused.
