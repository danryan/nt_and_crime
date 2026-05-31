# ENVGEN port brainstorm

- Vendor pin: `7800d929`. Issue: #45 (5 of 12 in #36). GUID: `OCEG`.
- Status: ready to spec. Quad-channel OC::App; gate-triggered modulation envelopes.

## What ENVGEN is

"Piqued / 4x EG": four independent `peaks::MultistageEnvelope` channels (AD / ADSR
/ ADR / etc), each gate/trigger driven, with per-channel euclidean-rhythm gating and
a trigger-delay queue. Output is unipolar modulation envelopes (0V..+5V), one per
DAC channel A-D, like BBGEN's gated balls.

## Vendor trace (`APP_ENVGEN.h`)

- Base: `class EnvelopeGenerator : settings::SettingsBase<EnvelopeGenerator, ENV_SETTING_LAST>`.
  File-scope singleton is `QuadEnvelopeGenerator envgen;` holding
  `EnvelopeGenerator envelopes_[4]` plus a `ui` struct (cursor +
  `OC::EuclideanMaskDraw` + selected channel). QUAD app -> BBGEN quad-facade
  template (NOT single-instance).
- `ENV_SETTING_LAST == 33` settings per channel (TYPE..INVERTED). Quad flatten ->
  `4 * 33 == 132` NT parameter rows. All settings are U4/U8/I16 int16-safe.
- Output: `ENVGEN_isr` -> `QuadEnvelopeGenerator::ISR` updates the four channels to
  `DAC_CHANNEL_A..D` via `OC::DAC::set(dac_channel, value)`. Unipolar envelope codes
  (0V..+5V through `route_cv_output`). Gate-triggered (TR in 1..4 default), not
  free-running.
- customUI: `ENVGEN_handleButtonEvent` reads only `EVENT_BUTTON_PRESS` (no
  `EVENT_BUTTON_LONG_PRESS`) -> `dispatch_custom_ui_factory<false>`.
- Thunks present: init, storageSize, save, restore, handleAppEvent, loop, menu,
  screensaver, handleButtonEvent, handleEncoderEvent, isr. The button helpers
  (top/lower/left/right) are defined BEFORE handleButtonEvent in the vendor header,
  so no forward declaration is needed (unlike FPART).
- Blob: `ENVGEN_save` packs `4 * EnvelopeGenerator::storageSize()`. Confirm it fits
  `kMaxBlobBytes = 512` at build (the runtime guards `storage_size > kMaxBlobBytes`).

## Cross-cutting decision (user-approved): flatten all 132 rows

Raise `kMaxSettings` 80 -> 160 in `_per_app_runtime.h`. Empirically verified the
raise leaves `.text`/`.data`/`.bss` byte-identical for existing apps (H1200
text=27344, POLYLFO text=29674 both before and after); it only grows `req.sram`
(the per-instance `parameters_storage`/`v_storage` member arrays, allocated in the
runtime SRAM pool via `sizeof`), so the ~82 KB `.text` scan-time cap is untouched.
On-device consequence is only the documented SRAM-cache reboot: after deploying the
enlarged build, already-shipped apps fail to ADD until one power cycle re-caches
`calculateRequirements`. User approved the flatten over the FPART-style 1-channel
subset.

## Layer 0 (this port builds; reusable by SEQ #52)

1. `menu::DrawMask<rtl, max_bits, height, padding>` (both overloads): the
   euclidean-rhythm mask widget, dropped from the shim `OC_menus.h` at foundation
   time. Hand-port from vendor `OC_menus.h:189-230` (pure `graphics.drawRect`). The
   vendor `OC_euclidean_mask_draw.h` (`OC::EuclideanMaskDraw`, header-only) calls
   `menu::DrawMask<false,32,7,1>` and `EuclideanPattern`; with DrawMask + bjorklund
   in scope it compiles unmodified. Reusable by SEQ.
2. `EuclideanPattern` is already shim-owned (`shim/src/cv_map/bjorklund.cpp`,
   aggregated via `oc_shim_impl.h`); vendor `bjorklund.h` (decls + inline `rotl32`)
   compiles on host and the signatures match. No vendor bjorklund `.cpp` dep.

## Additive shim strings (expected, per BBGEN lesson)

`OC::Strings::envelope_shapes[11]`, `reset_behaviours[5]`
(`"None","SP","SLP","SL","P"`), `falling_gate_behaviours[2]`
(`"Ignor","Honor"`). Add to shim `OC_strings.h` + `globals.cpp` (vendor
`OC_strings.cpp:131,156,160`). `trigger_delay_modes` is app-local
(`APP_ENVGEN.h:715`, no `OC::Strings::` prefix), not shim work.

## Vendor `.cpp` deps

`VENDOR_DEPS_ENVGEN := peaks_multistage_envelope.o peaks_resources.o`
(`peaks_multistage_envelope.cpp` reads `lut_env_increments` from
`peaks_resources.cpp`, the same resource BBGEN already links). Host srcs likewise.
`extern/stmlib_utils_dsp.h` and `util/util_misc.h` are header-only (shim has a
`util_misc.h` shadow; the `-include util_math.h` host-rule force-include already
covers the standalone-vendor-`.cpp` util_math hazard).

## Test focus

BBGEN-style: factory/registration (GUID OCEG, 132 settings via the quad facade),
quad-facade param count (`kIoParamCount + 132`), gate-triggered output (raise a
channel's trigger bus, assert its envelope rises and stays in 0V..+5V; an ungated
channel stays at 0V), settings round-trip through the blob, param-sync bidirectional.

## Scope / aborts

In scope: ENVGEN `.cpp`, manifest, DrawMask, the string tables, the kMaxSettings
raise, the peaks deps, host test. Abort if the shim prereq list far exceeds the
forecast (boundary mis-drawn), or if `peaks_multistage_envelope.cpp` needs a
non-shadowable Teensy header.
