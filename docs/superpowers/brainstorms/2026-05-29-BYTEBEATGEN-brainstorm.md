# BYTEBEATGEN OC::App Port Brainstorm

Date: 2026-05-29
Status: design approved, proceeding to spec
Vendor pin: `7800d929` (`vendor/O_C-Phazerville`)
Issue: #43 (order 3 of 12 on the OC::App track #36)
Branch: `dr/oc-app-bytebeatgen`

## Scope

Port the vendor full-screen O_C app `APP_BYTEBEATGEN.h` ("Byte Beats") to one NT
plug-in `.o` under `plugins/apps/`, on the O_C apps foundation (#29). One app, one
PR. Sequential track: this port inherits the BBGEN quad-facade and surfaces the
`kMaxSettings` raise + the conditional-menu mapping for later apps.

## Vendor facts (traced at pin `7800d929`)

- Base class: `class ByteBeat : public settings::SettingsBase<ByteBeat, BYTEBEAT_SETTING_LAST>`.
  Confirmed `OC::App`-style `SettingsBase`, NOT `HSApplication`.
- Container: `class QuadByteBeats` holding `ByteBeat bytebeats_[4]`, file-scope
  singleton `QuadByteBeats bytebeatgen`. Direct sibling of BBGEN's `QuadBouncingBalls`.
- Settings: `BYTEBEAT_SETTING_LAST = 19` per channel (EQUATION, SPEED, PITCH, P0,
  P1, P2, LOOP_MODE, LOOP_START/MED/FINE, LOOP_END/MED/FINE, TRIGGER_INPUT,
  STEP_MODE, CV1..CV4). 4 channels => 76 flat settings.
- Thunks: `BYTEBEATGEN_init`, `BYTEBEATGEN_storageSize` (= `4 * ByteBeat::storageSize()`),
  `BYTEBEATGEN_save`, `BYTEBEATGEN_restore`, `BYTEBEATGEN_handleAppEvent`,
  `BYTEBEATGEN_loop`, `BYTEBEATGEN_menu`, `BYTEBEATGEN_screensaver`,
  `BYTEBEATGEN_handleButtonEvent`, `BYTEBEATGEN_handleEncoderEvent`,
  `BYTEBEATGEN_isr`. Same shape as the BBGEN thunk set.
- Output: full-scale modulation. `Update()` writes
  `OC::DAC::set(dac_channel, OC::DAC::get_zero_offset(dac_channel) + (int16_t)b)`
  where `b` is the bytebeat sample. DAC_CHANNEL_A..D for the four channels. The
  16-bit code model already handles this (zero offset at 32768).
- Button handling: `EVENT_BUTTON_PRESS` only (UP/DOWN change selected segment, R
  toggles editing). No long-press path -> Low_rents customUI mapping template.
- Vendor `.cpp` dep: `peaks_bytebeat.cpp`. Includes only `peaks_bytebeat.h` and
  `extern/stmlib_utils_dsp.h`; no Teensy coupling. Links via `VENDOR_DEPS_BYTEBEATGEN`.

## Conditional menu (the new wrinkle vs BBGEN)

The vendor app keeps a per-channel enabled-settings list (`update_enabled_settings`,
`num_enabled_settings`, `enabled_setting_at`, `indentSetting`). When `LOOP_MODE` is
off, the six loop settings are hidden from the vendor menu; the encoder cursor end
is adjusted. This is purely a vendor customUI concern and stays intact in the port.

The NT parameter page is flat and static (built once at construct), so it cannot
mirror a runtime-varying enabled subset. Decision: expose ALL 4 * 19 = 76 settings
as flat NT parameter rows unconditionally, identical to how BBGEN exposed all 44.
The vendor titlebar + conditional-menu UX is preserved through the customUI; the NT
param page simply always shows the loop rows. No behavior regression.

## Load-bearing finding: kMaxSettings raise

`plugins/apps/_per_app_runtime.h` caps `kMaxSettings = 64`. BYTEBEATGEN needs 76
flat settings. This is the first app to exceed the cap. Raise `kMaxSettings` to 80
(76 needed plus headroom). Touches the shared runtime, so it is Layer 0 shared work
that lands first with the baseline still green.

Why not the FPART subset approach: FPART capped its NT params because its U32 chord
settings overflow `int16_t`. BYTEBEATGEN's settings are all U8/U4 (max 255), every
one int16-safe. The only constraint is `kMaxSettings`, so raising the cap is the
correct fix, not subsetting.

Blob: `BYTEBEATGEN_storageSize() = 4 * ByteBeat::storageSize()`. Vendor header
comment "TOTAL EEPROM SIZE: 4 * 16 bytes" = 64 bytes, well under `kMaxBlobBytes = 512`.
No blob cap change needed.

## Additive shim work (expected, per the FPART/BBGEN first-compile lesson)

1. `OC::Strings::bytebeat_equation_names[]` (16 entries: "hope", "love", "life",
   "age", "clysm", "monk", "NERV", "Trurl", "Pirx", "Snaut", "Hari", "Kris",
   "Tichy", "Bregg", "Avon", "Orac"). Mirrors vendor `OC_strings.cpp:127`. The
   EQUATION setting's `value_names`. Extern in `shim/include/OC_strings.h`, def in
   `shim/src/globals.cpp`.
2. `shim/include/util/util_misc.h`: minimal shadow providing only
   `util::reverse_byte` (vendor `util/util_misc.h:31`). The screensaver calls it.
   The vendor `util_misc.h` pulls `OC_options.h` and unrelated structs, so the shim
   provides just the one inline; the per-app `.cpp` includes the shim header.

Already shipped via BBGEN, reused as-is: `DIGITAL_INPUT_MASK`,
`OC::Strings::trigger_input_names`, `OC::Strings::no_yes`, `menu::QuadTitleBar`,
`SmoothedValue`, `SCALE8_16`, `USAT16`, `CONSTRAIN`, `drawAlignedByte`. `util::History`
(`util/util_history.h`) is vendor header-only and shim-compatible (needs only
`memset`, `DISALLOW_COPY_AND_ASSIGN`, `size_t`), no shadow.

## Recipe (frozen, per issue #43)

Copy `plugins/apps/BBGEN.cpp` (the quad-facade template):

- Quad `SettingsFacade` with captureless lambdas referencing the file-scope vendor
  singleton `bytebeatgen` and the `BYTEBEATGEN_save/restore/storageSize` thunks.
  Dispatch flat row `idx` to `bytebeats_[idx / 19]` setting `idx % 19`.
- `value_attr_at(idx)` returns `ByteBeat::value_attr(idx % 19)` (all channels share
  one attr table).
- Channel-prefixed names built once at construct into `g_names[76][16]`
  ("A Equation" .. "D CV4 -> "), supplied via the `param_name` hook.
- `numParameters = kIoParamCount + 76 = 88`.
- `construct_with_facade(*inst, &the_app, make_quad_facade(), 76)`.
- customUI: Low_rents button mapping (no long-press), encoders L/R forwarded,
  `push_settings_to_params` with `NT_parameterOffset()` on the push-back index.
- Factory in vendor field order (`tags` before `hasCustomUi`/`customUi`).
- GUID `OCBT` (shipped: OCLR, OCHA, OCSb, OCFP, OCBB).
- Forward-declare nothing extra: unlike FPART, BYTEBEATGEN defines its free
  functions before use, so no auto-prototype workaround is needed.

## Test focus

- Round-trip: every one of 76 settings survives `apply -> get`, gap-free.
- Param table: `numParameters == 88`; `parameters[base].name == "A Equation"`,
  `parameters[base+75].name == "D CV4 -> "`; name buffer bounds.
- Full-scale output: per-channel DAC writes land in 0V..+5V code space (not railed,
  not the pitch /1536 path). Routed-vs-unrouted channel separation.
- Conditional menu state: `update_enabled_settings` toggles enabled count when
  LOOP_MODE flips (13 with loop off, 19 with loop on), exercised via the test seam.
- 10x clocked multiplier: output is a continuous sample, not a per-edge counter, so
  movement/range assertions are valid (not fire-count).

## Out of scope

- Conditional NT-param hiding (flat table is intentional, documented above).
- ARDUINO_TEENSY41 CV5..CV8 mapping branch (NORTHERNLIGHT/non-Teensy path only).
- Hardware ADD smoke check (post-PR, needs physical device; tracked under #55).

## Per-entry verification (3 random settings, traced to vendor)

1. Row 0 (channel A, setting 0 = EQUATION): vendor `SETTINGS_DECLARE` entry 0 is
   `{ 0, 0, 15, "Equation", bytebeat_equation_names, STORAGE_TYPE_U8 }`. Flat NT row
   0 -> `bytebeats_[0]` setting 0, name "A Equation", min 0 max 15 def 0. Matches.
2. Row 25 (channel A=1? no: 25 / 19 = 1 r 6 -> channel B, setting 6 = LOOP_MODE):
   vendor entry 6 `{ 0, 0, 1, "Loop mode", no_yes, STORAGE_TYPE_U8 }`. Flat NT row
   25 -> `bytebeats_[1]` setting 6, name "B Loop mode", min 0 max 1 def 0. Matches.
3. Row 75 (75 / 19 = 3 r 18 -> channel D, setting 18 = CV4): vendor entry 18
   `{ NONE, NONE, LAST-1, "CV4 -> ", bytebeat_cv_mapping_names, STORAGE_TYPE_U4 }`.
   Flat NT row 75 -> `bytebeats_[3]` setting 18, name "D CV4 -> ". Matches.

All three trace clean to the vendor `SETTINGS_DECLARE` block.
