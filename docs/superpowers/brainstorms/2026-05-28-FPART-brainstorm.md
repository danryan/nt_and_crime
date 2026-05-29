# FPART (APP_FPART) O_C app port: brainstorm

- Vendor pin: `7800d929` (`vendor/O_C-Phazerville`)
- Issue: #41 (port FPART, position 1 of 12 in the #36 sequential track)
- Status: implemented; host tests green; ARM `.text` 24 KB

## Scope

Port the vendor O_C full-screen app FPART ("4 Parts": a 4-voice chord-step
sequencer with a staff-like display, by Jesse Dinneen) to one NT plug-in `.o`
under `plugins/apps/`, on the O_C apps foundation (#29), following the
Low_rents / Harrington1200 recipe.

## Vendor trace (APP_FPART.h at pin 7800d929)

- Base class: `class Fpart : public settings::SettingsBase<Fpart, FPART_SETTING_LAST>`.
  Confirmed `OC::App`-style `SettingsBase`, not `HSApplication`.
- File-scope singleton: `Fpart fpart_instance;` (line 540), like Low_rents'
  `lorenz_generator` and Harrington's `h1200_settings`.
- Thunks (all present, vendor field order): `FPART_init`, `FPART_storageSize`,
  `FPART_save`, `FPART_restore`, `FPART_handleAppEvent`, `FPART_loop`,
  `FPART_menu`, `FPART_screensaver`, `FPART_handleButtonEvent`,
  `FPART_handleEncoderEvent`, `FPART_isr`.
- Settings: `FPART_SETTING_LAST == 109`. Head settings 0..9 (root, scale,
  loop start, loop end, A-D octaves, long-L toggle, active chord); chord ints
  10..108 (99 of them).
- Output: pitch, 1V/oct, 4 voices via `OC::DAC::set_pitch(DAC_CHANNEL_A..D, ...)`
  in `FPART_isr` (lines 415-418).
- Inputs: `FPART_isr` reads 4 digital inputs (step back/forward, jump to loop
  start/end) and 2 ADC channels (absolute chord select, in-loop select).
- Vendor `.cpp` deps: none. The scale/root lookup tables are inline in the
  header; all included headers are shim-shadowed or header-only.

## Categorization: load-bearing finding (audit)

The issue's Layer 0 assumed all 109 settings become NT parameters and named the
blocker as the `kMaxSettings` cap (64 -> 109). The audit surfaced a different,
load-bearing fact:

- The 99 chord ints are `STORAGE_TYPE_U32` with range `10101010..32323232`.
- NT parameters (`_NT_parameter` min/max/def) and `_NT_algorithm::v` are
  `int16_t` (max 32767). The chord ints cannot be NT parameters without
  truncation and corruption.

Decision (confirmed with the maintainer): expose only the 10 int16-safe head
settings as NT parameters. The 99 chord ints stay app-internal, edited through
the staff-page customUI (the vendor encoder handlers) and persisted whole via
the `SettingsBase::Save/Restore` blob. This matches the vendor app, whose own
parameter menu draws only settings 0-9 (`APP_FPART.h:730`).

Consequence for Layer 0: the real shared blocker is not `kMaxSettings` (only 10
params are exposed, well under the 64 cap) but `kMaxBlobBytes`. The full
`SettingsBase::Save` blob is 406 bytes (10 head bytes + 99 U32 chord ints); the
runtime's `kMaxBlobBytes` was 256, so `serialise()`/`deserialise()` bailed at
the `storage_size() > kMaxBlobBytes` guard and silently dropped persistence.
Raise `kMaxBlobBytes` to 512.

## Exclusions

- Chord ints as NT parameters (int16 range; corruption). App-internal instead.
- No new vendor `.cpp` dependency.
- Hardware smoke check (ADD on device via the OCFP GUID) is post-PR: it needs
  physical access.

## Two or three most important rules carried in

- Per-app `.o`, vendor source never edited; compat via `shim/include/` shadowing
  and include-guard poison.
- Pitch output via `pitch_to_dac` (1V/oct), never the retired `/1536` model.
- Verify the plug-in ADDs on hardware, not just that it registers.
