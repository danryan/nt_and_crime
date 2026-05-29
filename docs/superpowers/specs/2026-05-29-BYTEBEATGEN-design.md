# Design: BYTEBEATGEN OC::App Port

Date: 2026-05-29
Status: approved
Vendor pin: `7800d929`
Issue: #43
Branch: `dr/oc-app-bytebeatgen`

## Context

Third app on the sequential OC::App port track (#36). `APP_BYTEBEATGEN.h` is a
quad-channel bytebeat generator: a file-scope `QuadByteBeats bytebeatgen` holding
`ByteBeat bytebeats_[4]`, each a `settings::SettingsBase<ByteBeat, 19>`. Direct
sibling of BBGEN (`QuadBouncingBalls`), so it reuses the quad-facade machinery
shipped in #42. Two things are new: 76 flat settings exceed the runtime
`kMaxSettings` cap, and the app carries a conditional menu.

## Decisions

- Reuse BBGEN quad facade - `construct_with_facade` + `param_name` hook already
  exist; BYTEBEATGEN wires a 76-row quad facade dispatching `idx` to
  `bytebeats_[idx/19]` setting `idx%19`.
- Expose all 76 settings flat - the NT param table is static; it cannot mirror the
  vendor's runtime-varying enabled-settings subset, so all 4*19 rows are always
  present. Vendor conditional menu stays in the customUI.
- Raise `kMaxSettings` 64 -> 80 - 76 needed plus headroom. Shared runtime change,
  lands first (Layer 0) with baseline green. Not the FPART subset approach, because
  every BYTEBEATGEN setting is int16-safe (max 255); only the cap blocks it.
- Full-scale modulation output - the vendor `Update()` writes
  `get_zero_offset(ch) + (int16_t)b`; the 16-bit DAC code model handles it. No
  output-scaling code in the port.
- GUID OCBT - unique against shipped OCLR/OCHA/OCSb/OCFP/OCBB.
- Low_rents customUI mapping - BYTEBEATGEN handles `EVENT_BUTTON_PRESS` only, no
  long-press, so the Low_rents button-release mapping applies (not Harrington1200).

## Architecture

Single TU `plugins/apps/BYTEBEATGEN.cpp` (`#define NT_OC_APP_TU 1`) aggregating the
OC shim via `_per_app_runtime.h`. Mirrors `plugins/apps/BBGEN.cpp` field-for-field.

```
plugins/apps/BYTEBEATGEN.cpp        (the port: facade, factory, customUI glue, seams)
shim/include/oc_app_manifests/BYTEBEATGEN.h   (guid OCBT, name "Byte Beats", bus rows)
shim/include/OC_strings.h           (+ extern bytebeat_equation_names[])
shim/src/globals.cpp                (+ bytebeat_equation_names[] definition)
shim/include/util/util_misc.h       (new: util::reverse_byte inline)
plugins/apps/_per_app_runtime.h     (kMaxSettings 64 -> 80; shared)
Makefile                            (OC_APP_LIST += BYTEBEATGEN; VENDOR_DEPS_BYTEBEATGEN)
harness/tests/test_oc_app_BYTEBEATGEN.cpp   (Catch2 behavior coverage)
```

### Facade dispatch

`kNumChannels = 4`, `kSettingsPerChannel = BYTEBEAT_SETTING_LAST = 19`,
`kNumSettings = 76`.

```cpp
f.get_value   = [](void* self, int idx) -> int {
    return static_cast<QuadByteBeats*>(self)
        ->bytebeats_[idx / 19].get_value(idx % 19); };
f.apply_value = [](void* self, int idx, int v) -> bool {
    return static_cast<QuadByteBeats*>(self)
        ->bytebeats_[idx / 19].apply_value(idx % 19, v); };
f.value_attr_at = [](int idx) { return &ByteBeat::value_attr(idx % 19); };
f.save = [](void*, void* b) { return BYTEBEATGEN_save(b); };
f.restore = [](void*, const void* b) { return BYTEBEATGEN_restore(b); };
f.storage_size = [] { return BYTEBEATGEN_storageSize(); };
f.param_name = [](void*, int idx) { return g_names[idx]; };
f.instance = &bytebeatgen;
```

### Parameter table

`numParameters = kIoParamCount(12) + kNumSettings(76) = 88`. Names built once at
construct into `char g_names[76][16]`, channel prefix `('A'+ch) + ' '` then the
truncated vendor `value_attr(s).name` (16 - 2 prefix - 1 null = 13 max).

### customUI

Copy the BBGEN `customUi_impl`: iterate `button_mapping_table`, on release classify
and `emit_button`; forward encoder L/R; `push_settings_to_params` (with
`NT_parameterOffset()` added to the global push-back index); then
`oc_runtime::customUi`. The vendor `BYTEBEATGEN_handleEncoderEvent` runs the
conditional-menu cursor adjust internally; no extra port wiring.

## Implementation notes

- `serialise`/`deserialise` are the runtime factories (addNumber-only blob). Blob is
  64 bytes, under the 512 cap.
- isr cadence loop already has the `sr==0` guard. No port change.
- Forward declarations: none needed. BYTEBEATGEN defines its free functions before
  use (unlike FPART).
- `util::History<uint8_t,64>` (screensaver) is vendor header-only, shim-compatible.

## Verification

- `make test-oc-app-BYTEBEATGEN` (host Catch2) green.
- `make build/arm/BYTEBEATGEN.o`: `.text` 16-20 KB, well under the 82 KB cap.
- `arm-none-eabi-nm build/arm/BYTEBEATGEN.o | grep ' U '`: no unresolved beyond the
  firmware contract; `peaks::ByteBeat::*` resolved (peaks_bytebeat.o linked).
- Existing OC-app + applet suites still green (kMaxSettings raise is additive).
- Post-PR hardware ADD smoke check for GUID OCBT (`mcp__nt_helper__new`).

## Spec footer

Recipe spot-check: the BBGEN.cpp template compiles and ships; BYTEBEATGEN differs
only in counts (19 vs 11 settings/channel), the equation-names string, util_misc,
and the kMaxSettings raise. Recipe holds.

Per-entry verification: 3 settings traced to vendor `SETTINGS_DECLARE` in the
brainstorm (rows 0, 25, 75 -> EQUATION/LOOP_MODE/CV4). All three match. 0 of 3
contradict.

Shim prereq verification: `bytebeat_equation_names` (new), `util::reverse_byte`
(new util_misc shadow), `kMaxSettings=80` (runtime). All other referenced shim
symbols (`DIGITAL_INPUT_MASK`, `trigger_input_names`, `no_yes`, `QuadTitleBar`,
`SmoothedValue`, `SCALE8_16`, `USAT16`, `drawAlignedByte`, `util::History`) verified
present at pin via grep before writing the plan.
