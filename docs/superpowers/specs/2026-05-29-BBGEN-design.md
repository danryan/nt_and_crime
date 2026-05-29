# BBGEN (APP_BBGEN) O_C app port design

Date: 2026-05-29
Vendor pin: `vendor/O_C-Phazerville` at `7800d929` (verify with
`git ls-tree HEAD vendor/O_C-Phazerville`).
Issue: #42 (Port BBGEN, order 2 of 12). Foundation: #29. Track: #36.
GUID: `OCBB` (shipped: OCLR, OCHA, OCSb, OCFP).

## Goal

Port the vendor `OC::App` app BBGEN (`APP_BBGEN.h`, "Bouncing balls") to a
disting NT plug-in on the O_C apps foundation, as one small `.o` under
`plugins/apps/`. BBGEN is the first quad-channel `OC::App` port: its app object
is a container of four `BouncingBall` settings instances, not the single
`SettingsBase` singleton the foundation runtime was built around. The quad
facade and the modulation-DAC helpers this port adds are the reusable Layer 0
that BYTEBEATGEN (#43) inherits.

## Vendor facts (traced at pin 7800d929)

- Base class: `class BouncingBall : public settings::SettingsBase<BouncingBall,
  BB_SETTING_LAST>` (`APP_BBGEN.h:63`). `OC::App`-style, not `HSApplication`.
- Container: `class QuadBouncingBalls` (`APP_BBGEN.h:222`) holds
  `BouncingBall balls_[4]` (line 283), a `ui` selection struct, and four
  `SmoothedValue` CV inputs. File-scope singleton `QuadBouncingBalls bbgen`
  (line 291).
- Settings: 11 per ball (`BB_SETTING_LAST == 11`): GRAVITY, BOUNCE_LOSS,
  INITIAL_AMPLITUDE, INITIAL_VELOCITY, TRIGGER_INPUT, RETRIGGER_BOUNCES,
  CV1, CV2, CV3, CV4, HARD_RESET (`APP_BBGEN.h:38-51`,
  `SETTINGS_DECLARE` at 208-220). Eight `STORAGE_TYPE_U8`, one
  `STORAGE_TYPE_U8` enum (TRIGGER_INPUT), four `STORAGE_TYPE_U4`
  (CV1..CV4 mappings), one U8 bool (HARD_RESET). All min/max within int16.
- Output: full-scale modulation. Each ball's `Update` writes
  `OC::DAC::get_zero_offset(ch) + bb_.ProcessSingleSample(gate,
  OC::DAC::MAX_VALUE - get_zero_offset(ch))` (`APP_BBGEN.h:184-185`). Unipolar
  envelope: range [zero_offset, MAX_VALUE].
- ISR (`APP_BBGEN.h:243-263`): pushes ADC_CHANNEL_1..4 into four SmoothedValues,
  reads `OC::DigitalInputs::clocked()`, calls `balls_[i].Update(triggers, cvs,
  DAC_CHANNEL_A..D)`.
- Per-ball trigger select: `BB_SETTING_TRIGGER_INPUT` picks which of
  DIGITAL_INPUT_1..4 gates that ball (`get_trigger_input`, used at
  `Update:171-181` with `DIGITAL_INPUT_MASK` and `read_immediate`).
- CV mappings: `BB_SETTING_CV1..CV4` map ADC channel n to one of
  grav/bnce/ampl/vel/retr or off (`apply_cv_mapping`, lines 124-145).
- `<PREFIX>_*` thunks: `BBGEN_init`, `BBGEN_loop`, `BBGEN_menu`,
  `BBGEN_handleButtonEvent`, `BBGEN_handleEncoderEvent`, `BBGEN_handleAppEvent`,
  `BBGEN_screensaver`, `BBGEN_isr`, plus quad persistence thunks
  `BBGEN_storageSize`/`BBGEN_save`/`BBGEN_restore` (`APP_BBGEN.h:293-413`).
- Button handling: reads only `UI::EVENT_BUTTON_PRESS` (line 359), no
  long-press path. Follows the Low_rents customUI mapping, not Harrington1200.
- Screensaver: `OC::scope_render()` (line 408).
- Vendor `.cpp` dep: peaks_bouncing_balls.h is header-only BUT pulls
  `peaks_resources.h`, and `BouncingBall` uses `lut_gravity`
  (`peaks_bouncing_balls.h:102`, `stmlib::Interpolate88(lut_gravity, gravity)`).
  `lut_gravity` is defined in `peaks_resources.cpp` (declared `extern` at
  `peaks_resources.h:52`, defined at `peaks_resources.cpp:114`). The port MUST
  link `peaks_resources.cpp`. (Issue #42's "Vendor .cpp deps: none" is
  incorrect; this design corrects it.)

## Frozen recipe (not redesigned)

Port one `OC::App` per `.cpp` under `plugins/apps/`, mirroring
`plugins/apps/Low_rents.cpp` (the modulation reference; no long-press) and the
foundation design `docs/superpowers/specs/2026-05-27-oc-apps-foundation-design.md`.
`plugins/apps/_per_app_runtime.h` is the runtime the port builds on. The
foundation runtime extensions below (quad facade, name override, DAC consts,
scope_render) are Layer 0 shared work, designed here, not improvised.

## Layer 0: shared shim + runtime additions

These land first (sequenced, baseline-green between), reused by BYTEBEATGEN.

### L0.1 Shim DAC modulation constants (`shim/include/OC_DAC.h`)

Add inside the shim `OC::DAC` namespace, mirroring vendor `OC_DAC.h:53,237`:

- `static constexpr uint16_t MAX_VALUE = 65535;`
- `inline uint32_t get_zero_offset(DAC_CHANNEL) { return 32768; }`

Rationale: NT 16-bit DAC convention is 0V at code 32768, +-5V full scale
(CLAUDE.md "OC DAC output is 16-bit code space"). A bouncing-ball envelope is
unipolar: vendor `value = zero_offset + ProcessSingleSample(..., MAX - zero)`
ranges [32768, 65535] = 0V..+5V. The existing runtime `route_cv_output` maps
`(code - 32768) / kCodesPerVolt`, so the envelope reaches the bus at 0V..+5V
without railing. Never the pitch-only `/1536` path. `get_zero_offset` ignores
the channel (NT has no per-channel calibration); the param exists for vendor
signature parity.

### L0.2 Shim `scope_render()` (`shim/include/OC_menus.h`, `shim/src/oc/menus.cpp`)

Add `void scope_render();` declaration and an implementation mirroring vendor
`OC_menus.cpp:126-154`, non-Teensy non-`NorthernLightModular` branch only:
`scope_averaging<11, 0x1f>()` then plot four quadrants
(`averaged_scope_history[0..3]` at x/64+x by 0/32+y). The shim already has the
`scope_averaging` template and `averaged_scope_history` ring
(`shim/src/oc/menus.cpp:122-143`); this reuses them. Covered by an addition to
the existing `test_oc_menus` host test.

### L0.3 Runtime quad-facade support (`plugins/apps/_per_app_runtime.h`)

The foundation `SettingsFacade` and templated `construct(alg, app, settings,
num_settings)` assume one typed `SettingsBase` instance. BBGEN needs a facade
whose 44 logical settings dispatch across four `BouncingBall` instances. Three
minimal, behavior-preserving changes:

1. Add an optional name-override hook to `SettingsFacade`:
   `const char* (*param_name)(void* self, int idx) = nullptr;`. Default null
   means "use `value_attr_at(idx)->name`" (current behavior).
2. `param_from_value_attr` gains an optional name-override argument; when
   non-null it overrides `va.name`. Existing callers pass nothing -> unchanged.
3. Extract the settings-table population loop from the templated `construct`
   into a non-template
   `construct_with_facade(AppAlgorithm&, const OC::App*, SettingsFacade,
   int num_settings)`. The templated `construct` becomes a thin wrapper:
   `construct_with_facade(alg, app, make_facade(settings), num_settings)`. The
   loop reads only `facade.value_attr_at`, `facade.get_value`, and (new)
   `facade.param_name`; no template dependency. This is a pure refactor:
   FPART, Low_rents, Harrington1200, StubApp construct paths are unchanged and
   their host tests stay green.

No change to `kMaxSettings` (64; BBGEN needs 44) or `kMaxBlobBytes` (512;
BBGEN blob is 4 * 9 = 36 bytes).

## Per-app unit: `plugins/apps/BBGEN.cpp`

Aggregating TU (`#define NT_OC_APP_TU 1`), mirroring Low_rents:

- Forward-declare nothing extra (BBGEN's thunks are all defined before use in
  the vendor header; unlike FPART it needs no auto-prototype workaround --
  verify during impl).
- `#define ENABLE_APP_BBGEN` before including the vendor header.
- Quad `SettingsFacade` built as a literal struct with captureless lambdas
  (they reference the file-scope `bbgen` global and the vendor `BBGEN_*`
  thunks, so no capture is needed):
  - `instance = &bbgen`
  - `get_value  = [](void* s, int i){ return ((QuadBouncingBalls*)s)->
    balls_[i / BB_SETTING_LAST].get_value(i % BB_SETTING_LAST); }`
  - `apply_value = [](void* s, int i, int v){ return ((QuadBouncingBalls*)s)->
    balls_[i / BB_SETTING_LAST].apply_value(i % BB_SETTING_LAST, v); }`
  - `value_attr_at = [](int i){ return &BouncingBall::value_attr(
    i % BB_SETTING_LAST); }` (all four channels share the same attrs)
  - `save / restore = [](...) { return BBGEN_save/BBGEN_restore(blob); }`,
    `storage_size = [](){ return BBGEN_storageSize(); }` (vendor quad thunks,
    4-ball blob)
  - `param_name = [](void*, int i){ return g_bbgen_names[i]; }` (see below)
- Channel-prefixed names: a file-scope `static char g_bbgen_names[44][16]`
  filled once at construct: `"<A|B|C|D> <vendor name>"` (e.g. "A Gravity").
  Longest vendor name "Bounce loss" (11) + 2-char prefix + null fits 16. The NT
  `_NT_parameter.name` pointer must outlive construct; a file-scope static
  satisfies this. Multiple instances share the buffer (names are identical), so
  no per-instance state. Build via a helper called from the factory construct
  thunk before `construct_with_facade`.
- `num_settings = 4 * BB_SETTING_LAST = 44`.
- `req.numParameters = oc_runtime::kIoParamCount + 44 = 56` (CLAUDE.md
  numParameters-must-match-range gotcha; 56 <= kMaxParams 76).
- `OC::App` built from the vendor `BBGEN_*` static thunks (App aggregate field
  order per foundation), `_NT_factory` in vendor field order
  (`tags` before `customUi`, CLAUDE.md), customUI emit-glue copied from
  Low_rents (no long-press; `kNT_encoderL`->on_encoder, `kNT_encoderButtonL`->
  on_button per the foundation standalone mapping; button events forwarded as
  `UI::EVENT_BUTTON_PRESS`).

## Manifest: `shim/include/oc_app_manifests/BBGEN.h`

Mirror FPART.h. 12-row I/O documentation: CV in 1..4 -> the four ADC channels
the ISR smooths (CV->param mappings); CV out A..D -> the four ball envelopes
(`DAC_CHANNEL_A..D`); TR in 1..4 -> the four `DIGITAL_INPUT_1..4` a ball's
TRIGGER_INPUT setting selects. `guid = NT_MULTICHAR('O','C','B','B')`,
`name = "Bouncing Balls"`.

## Build wiring (Makefile)

- `OC_APP_LIST := StubApp Low_rents Harrington1200 FPART BBGEN`
- `VENDOR_DEPS_BBGEN := build/arm/vendor_src/peaks_resources.o`
- `VENDOR_DEP_HOST_SRCS_BBGEN := $(HEM_SRC_DIR)/peaks_resources.cpp`

Follows the Low_rents precedent (`streams_resources.o`). The
`build/arm/vendor_src/%.o` rule (Makefile:223) and `BUILD_PER_OC_APP`
(Makefile:384, takes `VENDOR_DEPS_$(a)`) already support per-app vendor `.cpp`
linkage; the host `test_oc_app_%` rule (Makefile:601) pulls
`VENDOR_DEP_HOST_SRCS_$$*` via secondary expansion.

## Test: `harness/tests/test_oc_app_BBGEN.cpp`

TDD: write first. Concerns:

- Registration/construct: 56 parameters; rows [12,56) carry channel-prefixed
  names ("A Gravity" ... "D Hard reset"); enum rows (TRIGGER_INPUT, CV1..CV4,
  HARD_RESET) carry min-offset enum strings.
- Quad facade routing: editing global param row `12 + ch*11 + s` changes
  `bbgen.balls_[ch]` setting `s` only (idx/11, idx%11 mapping). Use
  `nt::set_parameter_offset(1)` per the customUI-pushback offset gotcha when
  exercising push-back.
- Output: drive a trigger, step the cadence, assert the routed CV-out bus value
  is within (0V, +5V] and non-zero (envelope fires), not railed at +5V
  constantly. The 10x clocked multiplier does NOT distort this: the envelope is
  continuous sample output, not a per-edge counter, so model the cadence
  accumulator, not a per-buffer fire count.
- Retrigger: second trigger edge restarts the envelope (amplitude rises again).
- Round-trip: serialise -> deserialise restores all four balls' settings
  (4 * 9 = 36-byte blob, under kMaxBlobBytes).
- scope_render screensaver: covered indirectly (draw at screensaver tick does
  not fault); the scope math itself is covered by the `test_oc_menus` addition.

## Out of scope

- BYTEBEATGEN (#43) and later apps: they inherit the quad facade + DAC helpers
  but ship in their own PRs.
- Per-channel DAC calibration (`get_zero_offset` is a fixed 32768).
- Hardware ADD smoke check: performed after PR open (needs physical NT);
  batched in #55. The port is not "done" until it ADDs on hardware via
  `mcp__nt_helper__new` with GUID OCBB (CLAUDE.md add-time hazards already
  handled by the runtime: serialise addNumber-only, sr==0 guard).

## Spec footer

### Recipe spot-check

The port follows the frozen `OC::App` recipe: aggregate TU, vendor `<PREFIX>_*`
thunks bound into an `OC::App`, `_NT_factory` in vendor field order, customUI
emit-glue copied from Low_rents, manifest + `OC_APP_LIST` + host test. The only
deviations are the three Layer 0 additions (quad facade, DAC modulation consts,
scope_render), each justified above against vendor source line numbers.

### Per-entry verification (3 traced end-to-end vs vendor source)

1. Settings count = 11. `BB_SETTING_LAST` enum closes at `APP_BBGEN.h:50` after
   11 named members (38-49); `SETTINGS_DECLARE` lists exactly 11 rows
   (208-220). num_settings = 4 * 11 = 44. CONFIRMED.
2. DAC write range. `Update:184` = `get_zero_offset(ch) + ProcessSingleSample(
   gate, MAX_VALUE - get_zero_offset(ch))`. With shim zero_offset=32768,
   MAX_VALUE=65535: range [32768, 65535] -> route_cv_output -> 0V..+5V
   unipolar. CONFIRMED.
3. Vendor `.cpp` dependency. `peaks_bouncing_balls.h:102` calls
   `stmlib::Interpolate88(lut_gravity, gravity)`; `lut_gravity` is `extern`
   (`peaks_resources.h:52`), defined in `peaks_resources.cpp:114`. Link of
   `peaks_resources.cpp` is REQUIRED. CONFIRMED (corrects issue #42).

All three trace clean; the issue's only factual error (no vendor .cpp dep) is
corrected in this spec. No further audit triggered.

### Shim prereq verification

- `SCALE8_16`, `USAT16`, `CONSTRAIN`: present in shim
  `shim/include/util/util_math.h:17,36,39`. CONFIRMED.
- `OC::DAC::set(DAC_CHANNEL, uint32_t)`: present `shim/include/OC_DAC.h:115`.
  CONFIRMED.
- `OC::DAC::MAX_VALUE`, `get_zero_offset`: ABSENT, added in L0.1.
- `OC::scope_render`: ABSENT (only `vectorscope_render`), added in L0.2.
- `scope_averaging` template + `averaged_scope_history`: present
  `shim/src/oc/menus.cpp:135,122`. CONFIRMED (L0.2 reuses).
- Quad facade / `construct_with_facade` / `param_name`: ABSENT, added in L0.3.
