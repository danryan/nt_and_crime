# Design: PASSENCORE NT plug-in

Date: 2026-05-31
Status: accepted
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #47

## Recipe (single-instance OC::App, MASK-excluded subset)

PASSENCORE follows the Harrington1200 single-instance template, with one
deviation: the settings facade exposes a non-contiguous subset (skipping the
U16 MASK setting at index 1).

### plugins/apps/PASSENCORE.cpp

1. `#define NT_OC_APP_TU 1` before `#include "_per_app_runtime.h"` to aggregate
   the OC shim impl into this TU.
2. Include the runtime, `oc_customui_dispatch.h`, the OC shim headers PASSENCORE
   references (`OC_apps.h`, `OC_ui.h`, `OC_core.h`, `OC_ADC.h`, `OC_DAC.h`,
   `OC_digital_inputs.h`, `OC_config.h`, `OC_strings.h`, `OC_menus.h`,
   `OC_bitmaps.h`, `OC_scales.h`, `Arduino.h`, `hem_graphics.h`,
   `util/util_settings.h`, `UI/ui_events.h`), the manifest, `distingnt/api.h`,
   `<new>`.
3. `namespace menu = OC::menu;` (the vendor app uses bare `menu::`).
4. `#define ENABLE_APP_PASSENCORE 1` then `#include "APP_PASSENCORE.h"`.
5. `struct PassencoreInstance : public oc_runtime::AppAlgorithm {};`
6. Build the `OC::App the_passencore_app` aggregate in OC::App field order from
   the `PASSENCORE_*` thunks; `reinterpret_cast` the two `UI::Event` handlers to
   the runtime `OcEventFn` (forward-declared `OC::UI::Event`), as AUTOMATONNETZ.
7. `calculateRequirements_impl`: `req.numParameters = kIoParamCount + (PASSENCORE_SETTING_LAST - 1)`;
   `req.sram = sizeof(PassencoreInstance)`.
8. `construct_impl`: placement-new, then build the subset facade and construct:

   ```cpp
   constexpr int kSkip = PASSENCORE_SETTING_MASK;            // 1
   auto phys = [](int i) { return i >= kSkip ? i + 1 : i; }; // logical -> physical
   oc_runtime::SettingsFacade facade =
       oc_runtime::make_facade(&passencore_instance);
   facade.get_value = [](void* self, int i) -> int {
       int p = i >= kSkip ? i + 1 : i;
       return static_cast<PASSENCORE*>(self)->get_value(static_cast<size_t>(p));
   };
   facade.apply_value = [](void* self, int i, int v) -> bool {
       int p = i >= kSkip ? i + 1 : i;
       return static_cast<PASSENCORE*>(self)->apply_value(static_cast<size_t>(p), v);
   };
   facade.value_attr_at = [](int i) -> const settings::value_attr* {
       int p = i >= kSkip ? i + 1 : i;
       return &PASSENCORE::value_attr(static_cast<size_t>(p));
   };
   // save/restore/storage_size keep the make_facade defaults (full 18-value blob).
   oc_runtime::construct_with_facade(*inst, &the_passencore_app, facade,
                                     PASSENCORE_SETTING_LAST - 1);
   ```

   `kSkip` is a compile-time constant, so the remap lambdas stay captureless
   (plain function pointers).
9. `_NT_factory` in api.h field order (`tags` before `hasCustomUi`/`customUi`).
   `.customUi = oc_runtime::dispatch_custom_ui_factory<false>` (no long press).
10. `pluginEntry` selector switch (version / numFactories / factoryInfo).
11. Test seams (declared non-static, no anonymous namespace):
    `pc_get_setting/pc_apply_setting` (logical row -> physical via the same skip
    remap so tests address the exposed rows), `pc_setting_count` (=17),
    `pc_settings_param_base`, `pc_get_outputs` (the four DAC codes), `pc_get_mask`
    / `pc_set_mask` (the excluded setting, for the persistence test),
    `pc_inject_trigger` (set a digital-input clock bit so the ISR samples a
    chord), `pc_arm_sentinel`.

### shim/include/oc_app_manifests/PASSENCORE.h

`guid = NT_MULTICHAR('O','C','P','S')`, name "Passencore". I/O: 2 CV in
(root/transpose CV3 and CV4 roles use ADC channels), 4 CV out (the four chord
voices), 4 trigger in (sample/target/passing/reset). Match the manifest field
shape used by AUTOMATONNETZ.

### shim/src/quant/OC_scales.cpp

Replace the 1-entry `scale_names[]` stub with the full vendor 99-entry table
(verbatim from vendor `OC_scales.cpp:259`). Keep `scale_names_short` as the stub
(unused by PASSENCORE).

### Makefile

- `OC_APP_LIST += PASSENCORE`.
- `VENDOR_DEPS_PASSENCORE := build/arm/vendor_src/OC_chords.o build/arm/vendor_src/OC_input_map.o`.
- `VENDOR_DEP_HOST_SRCS_PASSENCORE := $(HEM_SRC_DIR)/OC_chords.cpp $(HEM_SRC_DIR)/OC_input_map.cpp`.

## Test plan (harness/tests/test_oc_app_PASSENCORE.cpp)

- Factory/registration: `pluginEntry` returns the factory; GUID `OCPS`;
  `numParameters == kIoParamCount + 17`.
- Subset wiring: `pc_setting_count() == 17`; logical row 0 maps to SCALE, row 1
  maps to SAMPLE_TRIGGER (MASK skipped); the NT param at
  `settings_param_base + 1` carries SAMPLE_TRIGGER's attributes, not MASK's.
- Draw: `draw_factory` runs without fault.
- Chord output: inject a sample trigger, step, assert the four DAC outputs are
  valid pitch codes (non-degenerate; a triad spread). Acknowledge the 10x
  clocked multiplier is not in play here (OC apps drive the ISR via the cadence
  accumulator, not the Hemisphere Controller loop).
- Settings blob round-trip: set several exposed settings + set MASK via
  `pc_set_mask`, `serialise` then `deserialise`, assert all survive (MASK proves
  the excluded setting persists through the full-blob default hooks).
- Param sync bidirectional: a `parameterChanged` on an exposed row updates the
  physical setting; a customUI encoder edit pushes back to the NT param (drive
  `p->factory->customUi`, the only path that emits `UI::Event`s).
- Offset push-back: `nt::set_parameter_offset(1)`, exercise the push-back, assert
  the global index includes the offset (no off-by-one onto the row above).

## Footer

### Recipe spot-check

The recipe is the Harrington1200 single-instance template plus the BBGEN/
AUTOMATONNETZ facade-override pattern. Verified `construct_with_facade` reads
`value_attr_at(i)`/`get_value(self,i)`/`param_name` by logical index, so a
captureless remap in those three lambdas yields a coherent non-contiguous subset
(`_per_app_runtime.h:332-343`).

### Per-entry verification (3 settings traced against vendor)

- Row 0 (logical) -> phys 0 = `PASSENCORE_SETTING_SCALE`: vendor attr
  `{ SCALE_SEMI+1, SCALE_SEMI, NUM_SCALES-1, "scale", scale_names, U8 }`
  (`APP_PASSENCORE.h:952`). int16-safe (NUM_SCALES ~ 99). Exposed. OK.
- Row 1 (logical) -> phys 2 = `PASSENCORE_SETTING_SAMPLE_TRIGGER`: vendor attr
  `{ 0, 0, 3, "sample trigger", trigger_input_names, U4 }`
  (`APP_PASSENCORE.h:956`). int16-safe. Exposed at logical 1 (MASK skipped). OK.
- Physical 1 = `PASSENCORE_SETTING_MASK`: vendor attr
  `{ 65535, 1, 65535, "mask  -->", NULL, U16 }` (`APP_PASSENCORE.h:954`). Max
  65535 > int16 max 32767. Correctly excluded. OK.

3 of 3 consistent with vendor source.

### Shim prereq verification

- `oc_shim_impl.h:30-31` aggregates `braids_quantizer.cpp` + `OC_scales.cpp`
  into every OC-app TU; PASSENCORE needs no extra quant linkage.
- `OC_chords.cpp` / `OC_input_map.cpp` exist in vendor and reach only
  `util/util_macros.h` + shadowed `Arduino.h`; no ARM-only asm, no force-include.
- `OC::DUMMY` is `OC_scale_edit.h:19`; `OC::scale_names` requires porting the
  full vendor table into the shim (the one planned shim addition).
- GUID `OCPS` is unused (existing OC GUIDs: OCAN, OCBB, OCBT, OCEG, OCFP, OCHA,
  OCLR, OCPL, OCSb).
