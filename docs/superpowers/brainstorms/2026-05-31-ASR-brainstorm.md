# Brainstorm: port ASR (Analog Shift Register) to an NT plug-in

Date: 2026-05-31
Status: implemented (see ../specs/2026-05-31-ASR-design.md)
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #49 (part of the #36 remaining-OC-apps track)

## Why deferred

ASR is materially larger than the PASSENCORE/DQ templates: its INT_SEQ source
pulls a whole integer-sequence digit-table subsystem that must be hand-transcribed
into the shim (it lives in the unlinkable vendor `OC_strings.cpp`). That bulky
verbatim data work plus full `scale_names_short` is error-prone at the tail of a
large context, so the audit is captured here and implementation starts fresh.
This audit IS the deliverable for this step.

## App shape

Single-instance OC::App, the PASSENCORE template. The vendor keeps a file-scope
`ASRApp asr` singleton (a `settings::SettingsBase<ASRApp, ASR_SETTING_LAST>`, 27
settings) plus `ASRState asr_state` holding the customUI state
(`OC::ScaleEditor<ASRApp> scale_editor`, the menu cursor, `left_encoder_value`).

- Guard: `ENABLE_APP_ASR` (mechanical this time, unlike DQ's METAQ).
- Output: pitch. `ASR_isr` writes `OC::DAC::set((DAC_CHANNEL)i, outputs[i].get())`
  for the four channels (the shift register stages).
- customUI: the vendor `ASR_handleEncoderEvent` reads `EVENT_BUTTON_LONG_PRESS`
  (line 949), so the dispatch flag is `<true>`. ASR instantiates a vendor
  `OC::ScaleEditor`, so it needs the PASSENCORE `OC::UI`-alias (include
  `ui_events.h` before the runtime).
- `ASR_loop()` is empty, so SCALE (index 0) is not clobbered and stays exposed
  (the PASSENCORE precedent: exclude only the U16 mask).

## Settings model (subset, MASK excluded)

27 settings (`ASR_SETTING_SCALE` .. `ASR_SETTING_INT_SEQ_CV_SOURCE`). One is the
`STORAGE_TYPE_U16` mask `ASR_SETTING_MASK` at index 3 (range 1..65535), which
overflows int16 and is edited via the scale editor, so it is excluded. All others
are int16-safe (max 255). Mid-array skip: `phys = w < 3 ? w : w + 1`,
`num_settings = ASR_SETTING_LAST - 1 = 26`. The mask persists via the same
`SettingsBase::Save` (default `make_facade` blob hooks; ASR is single-instance, no
override needed, like PASSENCORE).

## Vendor dependencies

Includes: util_settings, util_trigger_delay, util_turing, util_ringbuffer,
util_integer_sequences (NEW), OC_ADC, OC_DAC, OC_menus, OC_scales, OC_scale_edit,
OC_strings, OC_visualfx, peaks_bytebeat (NEW, has `.cpp`), extern/dspinst.h.

- `braids_quantizer` + `OC_scales` ride the OC aggregation. `OC_scale_edit.h`,
  `OC_visualfx.h`, `util_turing.h`, `util_ringbuffer.h`, `dspinst.h` already
  proven portable (PASSENCORE/DQ/AUTOMATONNETZ).
- `peaks_bytebeat.cpp` is a vendor non-header-only dep (the ASR BYTEBEAT source).
  BYTEBEATGEN already links it: `VENDOR_DEPS_ASR := build/arm/vendor_src/peaks_bytebeat.o`,
  `VENDOR_DEP_HOST_SRCS_ASR := $(HEM_SRC_DIR)/peaks_bytebeat.cpp`.
- `util/util_integer_sequences.h` is header-only BUT quote-includes
  `"../OC_strings.h"` (the vendor sibling). The shim OC_strings poison
  (OC_STRINGS_H_) handles it ONLY if the shim `OC_strings.h` is included BEFORE
  `APP_ASR.h` in the per-app `.cpp` (the PASSENCORE include-order discipline). It
  also odr-uses the integer-sequence digit tables below.

## Shim additions (the bulk of the work)

1. Full `scale_names_short` table (99 entries, vendor `OC_strings.cpp:101`):
   currently a `{nullptr}` stub. ASR's SCALE setting uses `scale_names_short` as
   its enum `value_names`, so the stub reads out of bounds. Port verbatim, like
   `scale_names`. Reused by QQ/CHORDS later.
2. `OC::Strings::mult` (40 ratio labels, `OC_strings.cpp:145`),
   `integer_sequence_names` (10, `:135`), `integer_sequence_dirs` (2, `:139`).
3. `kIntSeqLen` constant (= 128) in shim `OC_strings.h` (vendor `OC_strings.h:6`).
4. The eight integer-sequence digit tables the `IntegerSequence` switch odr-uses,
   each `const uint8_t [kIntSeqLen]` (= 128 bytes), defined verbatim from vendor
   `OC_strings.cpp:168+` into shim `globals.cpp`:
   `pi_digits`, `van_eck`, `sum_of_squares_of_digits_of_n`, `digsum_of_n`,
   `digsum_of_n_base4`, `digsum_of_n_base5`, `count_down_by_2`,
   `interspersion_of_A163253`. (~1 KB of data; transcribe carefully and diff
   against vendor.) The commented-out tables (phi/tau/eul/rt2 digits) are not
   referenced and are not ported.

No new poison shadows expected beyond the existing OC_strings poison.

## Out of scope

- Host main-pitch fidelity if it hits the same calibrated-DAC vs shim 1V/oct gap
  as DQ (assert the path runs; defer pitch to hardware smoke).
- Hardware ADD smoke check (GUID candidate `OCAS`).

## Abort conditions

- More than 2 unforeseen poison shadows: halt.
- Any U16+ setting forced into an int16 param: halt.
- `.text` over the ~82 KB cap: halt (not expected; ~30 KB like the templates,
  plus ~1 KB of int-seq data).

## Verification footer (per-entry, 3 traced)

- Index 0 `ASR_SETTING_SCALE`: `{SCALE_SEMI, 0, NUM_SCALES-1, "Scale",
  scale_names_short, U8}` (APP_ASR.h:779). int16-safe. Exposed (logical 0).
- Index 3 `ASR_SETTING_MASK`: `{65535, 1, 65535, "mask", NULL, U16}`
  (APP_ASR.h:782). Overflows int16. Excluded.
- Index 4 `ASR_SETTING_INDEX`: `{0, 0, ASR_HOLD_BUF_SIZE-1, "buf.index", NULL,
  U8}` (APP_ASR.h:783). int16-safe. Exposed at logical 3 (mask skipped).

3 of 3 consistent with vendor source.
