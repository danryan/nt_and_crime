# O_C full-screen apps foundation: design

Date: 2026-05-27
Tracks: GitHub issue #29 (foundation)
Vendor pins:

- `vendor/O_C-Phazerville` at SHA `7800d929f25868f9a8b7d3d50514532ee001649b`
- `vendor/distingNT_API` at SHA `cd12d876dbe060859828053efab1cbc98c9df251`

## Goal

Build the shared shim foundation that lets vendor O_C (Ornament-and-Crime /
Phazerville) full-screen apps compile and run as independent Expert Sleepers
disting NT plug-ins, one app per plug-in, separate from the shipped Hemispheres
and Quadrants applet hosts. Prove the foundation end-to-end against two apps:
Low-rents (`APP_LORENZ.h`) and Harrington 1200 (`APP_H1200.h`). Expose each
app's settings as NT parameters in addition to the vendor customUI.

## Scope

Decided during the foundation brainstorm:

- Full foundation surface. Build all component areas listed below up front so
  later per-app ports inherit a complete shim.
- Two validation apps: Low-rents and Harrington 1200.
- NT-parameter add-on included. Both validation apps store a fixed settings
  array (Low-rents 10, Harrington 1200 37), so `numParameters` is static and no
  dynamic resizing is needed. Low-rents draws a flat menu. Harrington 1200 hides
  and shows settings in its menu through a dynamic enabled-settings list
  (`update_enabled_settings()`), but its storage and its NT parameter table stay
  fixed-size.

Per-app ports beyond these two are out of scope. They are spun out as separate
issues after the foundation lands.

## Architecture overview

Each app is its own NT plug-in compiling to one small `.o` under
`plugins/apps/<APP>.o`, mirroring the per-applet model. The plug-in embeds
exactly one vendor `OC::App` thunk table and points `OC::apps::current_app` at
it. The foundation shim drives that app from the NT plug-in entry points:

- NT `step()` drives `current_app->isr()` at the vendor audio cadence and pumps
  CV and trigger I/O.
- NT `draw()` drives `current_app->loop()`, then `DrawMenu()` or
  `DrawScreensaver()`.
- NT `customUi()` translates the `_NT_uiData` snapshot into the vendor
  `UI::Event` stream and feeds `HandleButtonEvent` / `HandleEncoderEvent`.

The vendor app body (`Controller`-equivalent logic, draw code, event handlers)
compiles unmodified. The firmware-facing layer the app depends on (hardware
display driver, ADC/DAC drivers, the `Ui` poll class, app registration) is
replaced by the shim.

## Key design decision: the menu layer is a hand-port, not a vendor compile

Issue #29 framed `OC_menus` widgets as code that "compiles once its deps
resolve." Investigation against vendor source shows that is not achievable. The
finding and the resolution:

- `OC_menus.h:28` does `#include "src/drivers/display.h"`. A quote-include
  resolves relative to the including file's own directory first, which is the
  vendor source tree. `-Ishim/include` is never consulted for that include, so
  the shim cannot shadow vendor `src/drivers/display.h`.
- Vendor `src/drivers/display.h` pulls `framebuffer.h`,
  `page_display_driver.h`, and `SH1106_128x64_driver.h` (the Teensy SH1106
  SPI/DMA display driver) and instantiates
  `PagedDisplayDriver<SH1106_128x64_Driver>`. None of that is satisfiable
  against a thin shim. This is why the existing project hand-wrote
  `shim::Graphics` rather than adopt vendor `weegfx`.

Resolution. The app translation unit lives under `plugins/apps/`, so its
bare-name include of `"OC_menus.h"` resolves through `-Ishim/include` first. A
hand-written `shim/include/OC_menus.h` shadows the vendor header wholesale, the
same shadowing pattern the shim already uses for `OC_DAC.h`, `OC_scales.h`, and
`streams_lorenz_generator.h`. The shim `OC_menus.h` reimplements the vendor menu
widgets on `shim::Graphics`, so vendor `display.h` is never pulled.

Rejected alternative: satisfy vendor `display.h` by stubbing the SH1106 driver
and framebuffer machinery so vendor `weegfx` and `OC_menus` compile unmodified.
Infeasible without reproducing the Teensy display driver surface, and it
duplicates the working `shim::Graphics`.

Consequence for the apps themselves: app bodies still compile unmodified. They
reference only `weegfx::coord_t` and `weegfx::kFixedFontW` (a typedef and a
constant) plus a small set of `graphics.*` methods, all provided by the shim.

## Foundation component contracts

### App lifecycle and dispatch

Provide `shim/include/OC_apps.h` declaring the vendor `OC::App` struct verbatim
(`vendor/.../OC_apps.h:49-68`) and the `OC::apps::` namespace:

- `extern const App *current_app;`
- `void Init(bool reset_settings);`
- `inline void ISR();` forwarding to `current_app->isr()`.
- `AppEvent` enum: `APP_EVENT_SUSPEND`, `APP_EVENT_RESUME`,
  `APP_EVENT_SCREENSAVER_ON`, `APP_EVENT_SCREENSAVER_OFF`
  (`vendor/.../OC_apps.h:32-37`).

Vendor `OC_apps.cpp:83` defines a `DECLARE_APP(a, b, name, prefix)` macro that
builds each `App` aggregate by token-pasting `prefix` onto eleven thunk names,
in this field order: `id`, `name`, `<prefix>_init`, `<prefix>_storageSize`,
`<prefix>_save`, `<prefix>_restore`, `<prefix>_handleAppEvent`,
`<prefix>_loop`, `<prefix>_menu`, `<prefix>_screensaver`,
`<prefix>_handleButtonEvent`, `<prefix>_handleEncoderEvent`, `<prefix>_isr`.
Each vendor app header defines those eleven functions with `static` (internal)
linkage; `OC_apps.cpp` collects every app into a single `available_apps[]`
table.

The shim does not compile `OC_apps.cpp`. The per-app runtime header
`plugins/apps/_per_app_runtime.h` replicates the `DECLARE_APP` expansion for
the one embedded app. It includes the vendor app header (which supplies the
eleven `static` thunks) and constructs the single `OC::App` aggregate in the
same translation unit, then sets `current_app` to it. The aggregate must be
built in the same TU as the app-header include because the thunks have internal
linkage; the per-app `.cpp` is that TU.

### Control router

Provide `shim/include/OC_ui.h` exposing only the `UiControl` enum
(`vendor/.../OC_ui.h:20-51`). The firmware `Ui` class (poll, debounce, encoder
acceleration, event queue, screensaver timeout, UI mode state machine) is not
ported. Its responsibilities are replaced by the NT dispatch in the runtime
header. `UI/ui_events.h` compiles unmodified and supplies the `UI::Event`
struct (`type`, `control`, `value`, `mask`) and `UI::EventType`
(`vendor/.../UI/ui_events.h:31-57`).

The router converts the per-call `_NT_uiData` snapshot
(`vendor/distingNT_API/.../api.h`: `pots[3]`, `controls`, `lastButtons`,
`encoders[2]`) into events:

- Encoders: `encoders[0]` to `CONTROL_ENCODER_L`, `encoders[1]` to
  `CONTROL_ENCODER_R`, emitted as `EVENT_ENCODER` with `value` set to the delta.
- Encoder pushes: `kNT_encoderButtonL` to `CONTROL_BUTTON_L`,
  `kNT_encoderButtonR` to `CONTROL_BUTTON_R`.
- Up and Down: `kNT_button3` to `CONTROL_BUTTON_UP`, `kNT_button4` to
  `CONTROL_BUTTON_DOWN`. Buttons 1 and 2 and the three pots stay unclaimed
  (firmware default).
- Edge detection: `controls XOR lastButtons` yields button down and up
  transitions.
- Long-press: the router times held duration and emits `EVENT_BUTTON_PRESS` on
  short release, `EVENT_BUTTON_LONG_PRESS` past a 500 ms threshold (the vendor
  threshold, `vendor/.../OC_ui.h`), then `EVENT_BUTTON_LONG_RELEASE`.
- Chords: `UI::Event.mask` is set to the live `controls` bitmask on every event,
  so vendor hold-as-shift gestures read `.mask` and work unmodified.

### Graphics extension and weegfx compat

Extend `shim::Graphics` (`shim/include/hem_graphics.h`) with the methods the
apps and the ported menu widgets call but the current shim lacks:

- `movePrintPos`, `print_right`, `write_right`
- `pretty_print`, `pretty_print_right`
- `drawHLine`, `drawHLinePattern`, `drawVLine`, `drawVLinePattern`
- `writeBitmap8`, `drawStr`, `drawAlignedByte`

Add a `weegfx` compat namespace providing `using coord_t = int_fast16_t;`,
`kFixedFontW = 6`, `kFixedFontH = 8`, and the `PIXEL_OP` and `CLEAR_FRAME`
enums (`vendor/.../weegfx.h:30-48`). The shim does not adopt vendor
`weegfx::Graphics`; the menu widgets and apps use `shim::Graphics` through the
existing `graphics` global.

Centering mechanism. The apps and the ported menu widgets call `graphics.*`
directly with vendor coordinates in `[0, 128)`. The existing `shim::Graphics`
writes raw to the 256-wide, 4-bit `NT_screen` and applies no offset (the
Hemisphere lane offset lives in the `HemisphereApplet` draw wrappers, which O_C
apps do not use). Rather than add an offset to the shared `shim::Graphics`, the
per-app runtime centers after the fact: the app draws into `[0, 128)`, then
`draw()` shifts each of the 64 rows right by 64 pixels into `[64, 192)` and
blanks the side margins. The shift is byte-aligned because `NT_screen` packs two
pixels per byte (128 bytes per row) and 64 pixels is exactly 32 bytes, so it is
a per-row `memmove` of 64 bytes from row offset 0 to offset 32 followed by
zeroing bytes `[0, 32)` and `[96, 128)`. This keeps vendor draw coordinates 1:1
with no scaling and touches no shared graphics code. Low-rents draws within
`[0, 128)` (the vectorscope plots at most `64 + 63`), so the shift covers its
output.

Also add `#define FASTRUN` to the shim `Arduino.h`. Low-rents declares
`void FASTRUN LORENZ_isr()`; the existing `PROGMEM` / `FLASHMEM` / `DMAMEM`
stubs are the precedent. The Hemisphere applets never used `FASTRUN`, so it is
absent today.

### Menu widgets

Provide `shim/include/OC_menus.h` reimplementing the vendor menu widgets on
`shim::Graphics`. Widgets and layout constants the validation apps use:

- `menu::ScreenCursor<screen_lines>`: `Init`, `AdjustEnd`, `Scroll`,
  `cursor_pos`, `first_visible`, `last_visible`, `editing`, `toggle_editing`,
  `set_editing`.
- `menu::SettingsList<screen_lines, start_x, value_x, end_x>`: `available`,
  `Next(SettingsListItem&)`, `AbsoluteLine`.
- `menu::SettingsListItem`: `selected`, `editing`, position fields, and the
  draw methods `DrawName`, `DrawDefault(int, value_attr)`,
  `DrawDefault(const char*, int, value_attr)`, `DrawCharName`, `DrawCustom`,
  `SetPrintPos`.
- `menu::TitleBar<start_x, columns, text_dx>` with aliases `DefaultTitleBar`
  (1 column, used by Harrington 1200) and `DualTitleBar` (2 columns, used by
  Low-rents): `SetColumn`, `Draw`, `Selected`, `DrawGateIndicator`,
  `ColumnStartX`.
- `menu::kScreenLines = 4`, `kMenuLineH = 12`, `kDefaultMenuStartX = 0`,
  `kDefaultValueX = 96`, `kDefaultMenuEndX = 126`, `CalcLineY`.
- `OC::vectorscope_render()` for the Low-rents screensaver. It calls
  `scope_averaging()`, which reads `OC::DAC::getHistory` (`OC_menus.cpp:116`), so
  the scope data comes from the shim DAC's output-history ring rather than a
  separate feed.
- `OC::visualize_pitch_classes(uint8_t*, coord_t, coord_t)` for the Harrington
  1200 tonnetz screensaver (vendor `OC_menus.cpp:65`, declared `OC_menus.h:38`).
  Hand-ported alongside the widgets.

Provide a minimal `shim/include/OC_bitmaps.h` declaring the few icon symbols the
ported widgets reference. Harrington 1200 includes `OC_bitmaps.h` but calls no
bitmap draw method, so the stub satisfies its include without bitmap data.

### I/O accessors

Provide shim accessors backed by the NT `inputs[]` and `outputs[]` bus arrays.
Both runtime and templated variants are required. Low-rents calls the templated
forms `OC::ADC::value<ADC_CHANNEL_1>()`, `OC::DAC::set<DAC_CHANNEL_A>()`, and
`OC::DigitalInputs::clocked<DIGITAL_INPUT_1>()`.

Channel representation (load-bearing). Vendor declares
`using ADC_CHANNEL = int; extern ADC_CHANNEL ADC_CHANNEL_1...` and
`using DAC_CHANNEL = int; extern DAC_CHANNEL DAC_CHANNEL_A...`, and the templated
accessors take the channel by reference (`template <ADC_CHANNEL &channel>`,
`template <DAC_CHANNEL &channel>`). The templated call requires each channel
symbol to be an extern lvalue object, not an enum constant. The current shim
`OC_DAC.h` declares `enum DAC_CHANNEL`, whose enumerators cannot bind to a
`DAC_CHANNEL &` template parameter. The foundation must switch the shim channel
representation to the vendor form (`using = int` plus `extern` channel objects
defined in a shim `.cpp`), keeping a `DAC_CHANNEL_LAST` constant for any array
bounds. The shim must not define `ARDUINO_TEENSY41` or `__IMXRT1062__`, so the
four-channel `#else` branches compile (Low-rents reads `ADC_CHANNEL_1..4`, not
`5..8`). This change touches the shared `OC_DAC.h` consumed by every Hemisphere
applet, so `make test-applets` must pass unchanged after it (see Risks).

- `shim/include/OC_ADC.h`: `OC::ADC::value(channel)` and `value<channel>()`,
  `pitch_value(channel)`, `raw_value(channel)`, `raw_pitch_value(channel)`
  (Harrington 1200 uses `raw_pitch_value`), and `ADC_CHANNEL_1..4` as extern
  objects. `pitch_value` uses the vendor 1V/oct scaling (12 semitones,
  `value >> 7` resolution).
- `shim/include/OC_digital_inputs.h`: `OC::DigitalInputs::clocked()`,
  `clocked(input)`, `clocked<input>()`, `read_immediate(input)`,
  `read_immediate<input>()`, `Scan()`, and the `DigitalInput` enum 1 through 4
  (an enum is fine here; the templates take it by value). `clocked()` reports
  rising edges since the last `Scan()`.
- Extend `shim/include/OC_DAC.h`: `OC::DAC::set(channel, value)` and
  `set<channel>(value)`, `set_pitch(channel, pitch, octave_offset)`,
  `semitone_to_dac`, `pitch_to_dac`, `set_octave`, `value(index)`, `Update()`,
  plus `get_voltage_scaling(channel)` and `set_voltage_scaled_semitone<channel>`
  with the `OutputVoltageScaling` enum (Harrington 1200 calls both). The NT
  outputs are 1V/oct, so the shim collapses the alternate voltage scalings:
  `get_voltage_scaling` returns `VOLTAGE_SCALING_1V_PER_OCT` and
  `set_voltage_scaled_semitone` falls through to the standard semitone path. The
  existing `kOctaveZero = 5` bias is preserved. Also provide
  `getHistory(channel, uint16_t*)` and `kHistoryDepth`: the shim DAC keeps a
  per-channel ring of recent output values, pushed on each `set`, and
  `getHistory` copies it. The Low-rents screensaver needs this because
  `vectorscope_render` calls `scope_averaging()` which reads
  `DAC::getHistory` (`OC_menus.cpp:116`). Keep `DAC_CHANNEL_LAST` as a constant;
  it is used as an array bound (`averaged_scope_history`, `OC_menus.cpp:101`).
- `OC::CORE::ticks` is declared in `shim/include/OC_core.h` and defined in
  `globals.cpp`, but it is incremented only by the Hemisphere runtime
  (`_per_applet_runtime.h:203`). A standalone O_C app does not run that path, so
  the O_C runtime must increment `OC::CORE::ticks` itself, once per isr tick.
  Otherwise `millis()` / `micros()` (`Arduino.h:63,67` read `ticks`), Harrington
  1200's trigger delays, app timing, and the screensaver idle-timeout never
  advance.

### Settings and storage

`util/util_settings.h` compiles unmodified and supplies
`settings::SettingsBase<T, N>` with `Save(void*)` and `Restore(const void*)`
over an `int values_[N]` array (`vendor/.../util/util_settings.h:110-145`,
header-only, no EEPROM dependency). The shim maps the Save and Restore blob onto
the NT plug-in `serialise` and `deserialise` JSON path so presets persist the
full app state. The NT API provides exactly this:
`_NT_factory.serialise(self, _NT_jsonStream&)` and
`deserialise(self, _NT_jsonParse&)` (`api.h:492,498`), and the per-applet
runtime already wraps `OnDataRequest`/`OnDataReceive` the same way
(`_per_applet_runtime.h:272-291`). The vendor EEPROM `AppData` and `PageStorage`
dispatch is not ported.

### Vendor strings and constants

Two vendor support headers feed the apps and must resolve in the shim chain:

- `OC_strings.h`. The current shim stub declares `capital_letters`,
  `note_names`, `note_names_unpadded`, `scale_names`, `scale_names_short`.
  Harrington 1200 also references `OC::Strings::cv_input_names_none`,
  `OC::Strings::trigger_delay_times`, `OC::trigger_delay_ticks`, and the
  `kNumDelayTimes = 8` constant (`OC_strings.h:10`). Extend the shim
  `OC_strings.h` to declare these. The string tables and `trigger_delay_ticks`
  data are defined in `OC_strings.cpp`, which the Harrington 1200 plug-in
  already links.
- `OC_config.h`. Vendor `OC_config.h` is include-free but carries Teensy pin
  defines. The apps reach it only for constants: `OC_CORE_ISR_FREQ = 16666`
  (the isr cadence) and `kMaxTriggerDelayTicks = 96` (Harrington 1200's
  `TriggerDelays` bound). Because the shim shadows `OC_digital_inputs.h` (the
  header that pulls vendor `OC_config.h`), provide a minimal net-new shim
  `OC_config.h` exposing just those constants rather than the vendor pin map.

`OC::SemitoneQuantizer` (used by Harrington 1200) is already provided by the
shimmed `OC_scales.h`; the Hemisphere shim uses it too (`HSUtils.h:198`).
`OC::TriggerDelays` is header-only in `OC_trigger_delays.h`, whose only deps are
the shimmed `OC_digital_inputs.h` and the include-free
`util/util_trigger_delay.h`.

### NT-parameter add-on

Each app exposes an `_NT_parameter[]` table built from two sources:

- I/O routing params: four CV-input bus selects, four CV-output bus selects,
  four trigger-input bus selects, following the existing host I/O routing
  pattern.
- App settings params: one parameter per `SettingsBase` entry, derived from the
  vendor `value_attr` (name, min, max, default, and enum labels where present).

Enum-label indexing (latent correctness rule). The vendor menu indexes
`value_names[value]` absolutely (`OC_menus.h:306`), but NT enum parameters index
their string array min-relative (`strings[value - min]`). The shim must pass
`value_attr.value_names + value_attr.min_` as the NT string array while keeping
NT min and max at the vendor min and max, so `strings[value - min]` resolves to
`value_names[value]`. Both validation apps happen to use `min == 0` for every
enum setting (Low-rents `LORENZ_OUTPUT_X1` is 0; every Harrington 1200 enum row
is `{default, 0, max-1, ...}`), so neither exercises the offset. It is still a
required rule, because catalog apps with a nonzero enum `min` would otherwise
show shifted labels. The per-app tests cannot catch this; the generator must
apply the offset unconditionally.

`numParameters` is static (the I/O count plus the settings count). Both
validation apps store a fixed-size settings array, so the table never resizes.
The NT parameter page exposes every settings slot unconditionally. For
Harrington 1200 that means all 37 slots show even though its vendor menu hides a
subset at any moment; the param page does not mirror the menu's conditional
visibility, which is acceptable because customUI remains the primary UI and the
param page is the optional add-on. Two-way sync:

- `parameterChanged(i)` writes the new value into the app `values_[]` for
  settings params, or updates routing for I/O params.
- App-side edits (the encoder editing a setting through the vendor UI) push the
  changed value back to the NT store with `NT_setParameterFromUi`.

Both directions are guarded by a construct-time sentinel that flips true only
after the first `draw()`, because the firmware fires `parameterChanged` for
every parameter during construct before the algorithm is registered. This is the
hazard documented in CLAUDE.md (`Construct-time parameterChanged hazard`).
`NT_setParameterFromUi` does not re-enter `parameterChanged` synchronously, so
no stack re-entry guard is required. Enlarging the algorithm struct requires a
device power cycle to invalidate the firmware SRAM-size cache (test-plan note).

### Lifecycle cadence mapping

- `isr()`: the vendor ISR runs at 16.666 kHz (`OC_CORE_ISR_FREQ`). A tick
  accumulator in `step()` advances by `numFrames` each call and invokes `isr()`
  the number of times needed to hold a 16.666 kHz average against NT's actual
  sample rate. This preserves the vendor time base for LFOs, envelopes, and
  trigger delays. Calling `isr()` once per NT frame is rejected because it
  distorts the time base by roughly 3x at 48 kHz. Before each `isr()` the shim
  refreshes `OC::ADC` from `inputs[]` and runs `DigitalInputs::Scan()` with
  rising-edge detection on the trigger buses, and increments `OC::CORE::ticks`
  (the Hemisphere runtime does this at `_per_applet_runtime.h:203`; the O_C
  runtime must do it too). After each `isr()` the shim flushes `OC::DAC` values
  to `outputs[]`. The Hemisphere 10x-multiplier edge discipline applies: a single
  trigger edge must be seen by exactly one tick, not every tick in the buffer.
- `loop()`: called once per `draw()` for deferred work.
- `draw()`: increments an idle counter, runs `loop()`, selects
  `DrawScreensaver()` when idle past the timeout or `DrawMenu()` otherwise (both
  draw into `[0, 128)`), then runs the per-row 32-byte shift that centers the
  canvas into `[64, 192)` and blanks the margins. Any control event resets the
  idle counter.
- `HandleAppEvent`: the runtime fires `Init()` and `APP_EVENT_RESUME` at
  construct, and `APP_EVENT_SCREENSAVER_ON` / `APP_EVENT_SCREENSAVER_OFF` on
  idle transitions.

## Canonical recipe: porting one O_C app

This is the reusable pattern. A per-app port repeats these steps for its own
app.

1. Add `plugins/apps/<APP>.cpp` including the per-app runtime header and the
   vendor `APP_<NAME>.h`.
2. Add `shim/include/oc_app_manifests/<APP>.h` mirroring the
   `applet_manifest.h` shape: a `guid` (`NT_MULTICHAR` 4-char, with a distinct
   prefix from the Hemisphere `Hm` and the hosts `HmHh` / `QdHh`), `name`,
   `description`, and `inputs[]` / `outputs[]` `BusParam` lists (four CV in, four
   CV out, four trigger in). The runtime builds the I/O routing params from these
   lists. The per-setting params are derived from the app's `SettingsBase`
   `value_attr` at runtime, not from the manifest.
3. Construct the single `OC::App` aggregate in the runtime, replicating the
   vendor `DECLARE_APP` field order (`vendor/.../OC_apps.cpp:83`): `id`, `name`,
   then the app's `static` thunks `<PREFIX>_init`, `<PREFIX>_storageSize`,
   `<PREFIX>_save`, `<PREFIX>_restore`, `<PREFIX>_handleAppEvent`,
   `<PREFIX>_loop`, `<PREFIX>_menu`, `<PREFIX>_screensaver`,
   `<PREFIX>_handleButtonEvent`, `<PREFIX>_handleEncoderEvent`, `<PREFIX>_isr`.
   Build the aggregate in the same TU as the app-header include (the thunks have
   internal linkage) and set `current_app` to it.
4. Generate the `_NT_parameter[]` table from the app's `SettingsBase`
   `value_attr` plus the I/O routing params.
5. Add the app to `OC_APP_LIST` in the Makefile. Set `VENDOR_DEPS_<APP>` to any
   vendor `.cpp` the app links (empty when header-only).
6. Add `harness/tests/test_oc_app_<APP>.cpp` covering settings round-trip, isr
   output for known input, customUi event routing, and param sync.
7. Build `make build/arm/<APP>.o`, verify `.text` under the firmware cap with
   `arm-none-eabi-readelf -W -S`, and confirm no unresolved symbols with
   `arm-none-eabi-nm`.

## Per-app entries

### Low-rents (`APP_LORENZ.h`)

- Vendor source: 378 lines.
- Class: `LorenzGenerator : public settings::SettingsBase<LorenzGenerator,
  LORENZ_SETTING_LAST>`, 10 settings (`LORENZ_SETTING_FREQ1` through
  `LORENZ_SETTING_OUT_D`).
- Includes: `streams_lorenz_generator.h`, `util/util_math.h`,
  `OC_digital_inputs.h`. None pull vendor display.h.
- Graphics calls: `graphics.print`, `graphics.setPrintPos` only.
- Menu widgets: `ScreenCursor`, `SettingsList`, `SettingsListItem`,
  `DualTitleBar`.
- Screensaver: `LORENZ_screensaver()` calls `OC::vectorscope_render()`.
- I/O: 4 CV in (frequency and rho per generator), 4 CV out (mappable, 20
  combinations), 4 TR (resets, freeze).
- Vendor deps to link: the existing `build/arm/vendor_src/streams_resources.o`
  and `streams_lorenz_generator.o` (already built for the `LowerRenz` applet).
- `.text` expectation: small. This is the small canary.
- Test concerns: settings round-trip over the 10 fields; isr produces the
  expected DAC output for a known frequency and rho; the trigger-edge reset is
  seen exactly once per edge under the tick accumulator; param add-on
  bidirectional sync over the 10 settings.

### Harrington 1200 (`APP_H1200.h`)

- Vendor source: 1218 lines.
- Class: `H1200Settings : public settings::SettingsBase<H1200Settings,
  H1200_SETTING_LAST>`, 37 settings stored.
- Dynamic menu: `update_enabled_settings()` rebuilds an `enabled_settings_[]`
  list and `num_enabled_settings_` so the menu shows a conditional subset of the
  37 (for example the Euclidean settings appear only in the relevant mode). This
  exercises the customUI path with conditional visibility, which a flat NT param
  page cannot reproduce. Storage stays a fixed 37 slots.
- Includes: `OC_bitmaps.h`, `OC_strings.h`, `OC_trigger_delays.h`,
  `tonnetz/tonnetz_state.h`, `util/util_settings.h`, `util/util_ringbuffer.h`,
  `bjorklund.h`. None pull vendor display.h.
- Graphics calls: `graphics.print`, `graphics.printf`, `graphics.setPrintPos`,
  `graphics.movePrintPos`, `graphics.pretty_print`. References
  `weegfx::kFixedFontW` and `weegfx::coord_t` (provided by the compat
  namespace).
- Menu widgets: `ScreenCursor`, `DefaultTitleBar`, `SettingsList`,
  `SettingsListItem`.
- Screensaver: `H1200_screensaver()` renders a tonnetz note circle.
- I/O: CV1 (root), CV4 (inversion), 4 TR (reset, P, L, R), 4 CV out (root plus
  triad).
- Header-only deps: `tonnetz/tonnetz_state.h`, `tonnetz/tonnetz.h`,
  `tonnetz/tonnetz_abstract_triad.h`, `OC_trigger_delays.h`,
  `util/util_ringbuffer.h`.
- Vendor deps to link: `OC_strings.cpp` (string tables) is the only net-new
  one, compiled in place by the existing `build/arm/vendor_src/%.o` rule.
  Euclidean (`bjorklund`) is already a shim source
  (`shim/src/cv_map/bjorklund.cpp`, in `SHIM_CORE_SRCS`), so it is reused, not
  recompiled from vendor.
- `.text` expectation: heaviest of the two. This is the budget canary. The `.o`
  must stay under the firmware cap of roughly 82 KB `.text`.
- Test concerns: settings round-trip over the 37 fields; the tonnetz
  transformation output for known root and transform inputs; trigger-edge
  handling for the four transform triggers; the circle screensaver draws without
  faulting; param add-on bidirectional sync over the 37 settings.

## File layout and build wiring

New and changed files:

- `shim/include/`: add `OC_apps.h`, `OC_ui.h`, `OC_ADC.h`,
  `OC_digital_inputs.h`, `OC_menus.h`, `OC_bitmaps.h`, `OC_config.h`; extend
  `OC_DAC.h` (channel representation change), `OC_strings.h` (add
  `cv_input_names_none`, `trigger_delay_times`, `trigger_delay_ticks`,
  `kNumDelayTimes`), `hem_graphics.h` (12 weegfx methods plus the compat
  namespace), and `Arduino.h` (`#define FASTRUN`).
- `shim/src/oc/`: implementation for the extended graphics methods, the menu
  widgets that are not header-only, `vectorscope_render`,
  `visualize_pitch_classes`, the I/O accessor backing, and the `ADC_CHANNEL` /
  `DAC_CHANNEL` extern channel objects.
- `plugins/apps/_per_app_runtime.h`: the shared per-app runtime (app
  aggregate construction, NT entry-point glue, control router, cadence,
  param sync). For ARM it must aggregate the shim impl into the single app TU
  the way `hem_shim_impl.h:4-14` does (it `#include`s the `.cpp` bodies
  `globals.cpp`, `graphics.cpp`, `icons.cpp`, `cxx_runtime_stubs.cpp`,
  `cv_map/bjorklund.cpp`, `quant/*.cpp`). Provide a new OC aggregation header
  that pulls the OC component set (the above plus the new `shim/src/oc/*.cpp`)
  rather than reusing `hem_shim_impl.h`, which is coupled to the Hemisphere
  headers (`HemisphereApplet.h`, `HSIOFrame.h`, `HSClockManager.h`). This is why
  bjorklund and `SemitoneQuantizer` link with no VENDOR_DEPS entry: they ride
  the aggregation. `OC_strings.cpp` stays a separate `VENDOR_DEPS` object
  because vendor cpps are compiled standalone, not `#include`d.
- `plugins/apps/Low_rents.cpp`, `plugins/apps/Harrington1200.cpp`.
- `shim/include/oc_app_manifests/Low_rents.h`,
  `shim/include/oc_app_manifests/Harrington1200.h`.
- `harness/tests/test_oc_app_Low_rents.cpp`,
  `harness/tests/test_oc_app_Harrington1200.cpp`.
- `harness`: a `_NT_uiData` synthesis helper (build a snapshot, emit button
  down/up and long-press edge sequences, set encoder deltas) so the per-app
  router tests can call `factory->customUi`. The harness has no such driver
  today.
- `Makefile`: add `OC_APP_LIST`, a `BUILD_PER_OC_APP` macro paralleling
  `BUILD_PER_APPLET` (compile the one `.cpp`, partial-link `VENDOR_DEPS` plus
  `COMPILER_RT_OBJS`, `objcopy` the `.group` section, merge with
  `merge_sections.lds`, strip), per-app `VENDOR_DEPS_<APP>`
  (`VENDOR_DEPS_Harrington1200 := build/arm/vendor_src/OC_strings.o`;
  Low-rents reuses the existing `streams_*.o`), and the host and ARM test rules
  for the new app tests. Reuse the existing
  `build/arm/vendor_src/%.o: $(HEM_SRC_DIR)/%.cpp` rule for `OC_strings.cpp`. Add
  the new shim impl to `SHIM_CORE_SRCS` for host-test linking, defined above its
  first prerequisite use (the prerequisite-expansion-timing gotcha in CLAUDE.md).
  The `-Ishim/include` before `-I$(HEM_SRC_DIR)` ordering is load-bearing and
  unchanged.

## Validation plan and `.text` budget

- Host Catch2 per app via the `nt_runtime` simulator and the `plugin_loader`
  factory path, covering the four test concerns listed per app.
- New test idiom for `customUi`. The harness drives `step()` and `draw()`
  (`harness/src/main.cpp:326,344`) and `parameterChanged`
  (`harness/src/nt_runtime.cpp:227`), but it never constructs a `_NT_uiData` and
  has no `customUi` call site. The per-app router tests must drive it directly:
  load the factory, build a `_NT_uiData` (`pots`, `controls`, `lastButtons`,
  `encoders`), call `factory->customUi(algorithm, &uiData)`, and assert the
  routed `UI::Event` reached the app (cursor moved, setting changed). A small
  shared helper for synthesizing `_NT_uiData` and edge sequences belongs in the
  harness. This is net-new; the existing `*_test_inject_slot` seams cover host
  slot resolution, not control events.
- Router unit test (foundation-level, not per-app). The chord path
  (`event.mask`) and the full button matrix are foundation code that neither
  validation app exercises. Add a dedicated router test that feeds `_NT_uiData`
  sequences straight through the router and asserts the emitted `UI::Event`
  stream: short press to `EVENT_BUTTON_PRESS`, held to `EVENT_BUTTON_LONG_PRESS`
  then `EVENT_BUTTON_LONG_RELEASE`, encoder deltas, and a chord where `.mask`
  carries the live `controls` bitmask. This closes the chord coverage gap
  cheaply without a chord-using app.
- Settings round-trip is tested by calling the app `Save`/`Restore` in-TU (or
  the plug-in `serialise`/`deserialise` through `nt_jsonstream`), not through a
  pack helper.
- `make arm` builds both app `.o` files. Verify each `.text` size with
  `arm-none-eabi-readelf -W -S build/arm/<APP>.o` and confirm no unresolved
  symbols with `arm-none-eabi-nm build/arm/<APP>.o | grep ' U '` (expect only
  the firmware-resolved surface documented in CLAUDE.md).
- Hardware smoke after PR open: centered render at x-offset 64, encoder and
  button navigation with buttons 3 and 4 as Up and Down, screensaver entry and
  exit, CV input and output through the routing params, and a settings save and
  restore across a preset reload. Power cycle before the smoke check because the
  algorithm struct is new (SRAM-size cache rule).

Coverage boundary. The full-surface scope means the two validation apps exercise
only part of what the foundation builds. The acceptance set proves lifecycle
(including `APP_EVENT_RESUME` at construct and `SCREENSAVER_ON`/`OFF` on idle,
both exercised by Harrington 1200), the router (UP/DOWN/L/R plus long-press),
the menu widgets, settings round-trip, param sync, the four-in/four-out/four-TR
I/O, and centering. It does not exercise the chord path (`event.mask`), 6 of the
12 graphics methods (`drawVLine`, `drawVLinePattern`, `writeBitmap8`,
`write_right`, `drawStr`, `drawAlignedByte`), the enum-label offset (no nonzero
enum `min` in either app), or the alternate voltage scalings (collapsed to
1V/oct). Those are justified only by the broader catalog and are validated by
later per-app ports. `APP_EVENT_SUSPEND` is never fired in a single-app plug-in
(no app switch); this is harmless because Harrington 1200 treats it like the
screensaver events.

## Risks and mitigations

- Menu hand-port pixel fidelity. The ported widgets must match vendor layout.
  Mitigation: port the widget logic verbatim onto `shim::Graphics` and validate
  visually on hardware.
- isr cadence timing fidelity. Mitigation: the tick accumulator holds the vendor
  16.666 kHz average; host tests assert isr call counts per buffer.
- Harrington 1200 `.text` budget. Mitigation: it is the budget canary; if it
  overflows the cap, the per-applet shrink strategies in CLAUDE.md apply (kill
  unused vendor deps, Q15 tables, drop `printf`).
- Param construct-time `parameterChanged` and SRAM cache. Mitigation: the
  construct-time sentinel and the power-cycle test-plan note.
- Trigger-edge double counting under the tick accumulator. Mitigation: replicate
  the Hemisphere rising-edge detection so one edge is one tick.
- Shared-header regression. Switching `OC_DAC.h` from `enum DAC_CHANNEL` to the
  vendor `using = int` plus extern-object form changes a header every Hemisphere
  applet includes. Mitigation: keep `DAC_CHANNEL_LAST` for array bounds, define
  the channel objects in a shim `.cpp`, and gate the change on `make
  test-applets` passing unchanged plus a byte-identical `.text` check on a
  sample of existing applet `.o` files.
- `_NT_factory` designated-initializer order. The factory orders `tags` before
  `hasCustomUi` / `customUi` (`api.h:468`); the O_C factory uses `customUi`,
  `serialise`, `deserialise`, and `tags`, so an out-of-order initializer is
  ill-formed and the construct-time `parameterChanged` path can hard-fault on
  device. Mitigation: follow the field order in `plugins/probes/aeabi_probe.cpp`
  (the canonical factory reference in CLAUDE.md).

## Out of scope

- Per-app ports beyond Low-rents and Harrington 1200. Filed as separate issues
  after the foundation lands.
- System and utility apps: Setup/About, Calibr8or, Scale Editor, Waveform
  Editor, References, Backup.
- Hemispheres and Quadrants, already shipped as composer hosts.
- Conditional NT-parameter trees and dynamic `numParameters`. Both validation
  apps store a fixed-size settings array, so the NT parameter table never
  resizes. Harrington 1200's conditional menu visibility lives entirely in its
  customUI, not in the NT parameter table.

## Spec footer

### Recipe spot-check

The canonical recipe reproduces the established per-applet shape: one `.cpp`
plus one manifest header plus a Makefile list entry plus a per-app test, with
`VENDOR_DEPS_<APP>` for any linked vendor `.cpp`. It differs from the applet
recipe only in the runtime header (`_per_app_runtime.h` constructs an
`OC::App` aggregate and runs the customUI dispatch, where
`_per_applet_runtime.h` runs the Hemisphere applet contract). The shadowing of
`OC_menus.h` follows the same bare-name `-Ishim/include` precedence the shim
already relies on for `OC_DAC.h` and `OC_scales.h`.

### Per-entry verification

All facts below traced against vendor source at SHA `7800d929`.

App registration:

- `DECLARE_APP(a, b, name, prefix)` exists at `OC_apps.cpp:83` and binds the
  eleven thunks in the App-field order quoted in the App lifecycle section.
  Verified.
- Low-rents thunks are `static` and defined in `APP_LORENZ.h`: `LORENZ_init`
  (244), `LORENZ_storageSize` (250), `LORENZ_save` (254), `LORENZ_restore`
  (258), `LORENZ_loop` (262), `LORENZ_menu` (265), `LORENZ_screensaver` (289),
  `LORENZ_handleAppEvent` (293), `LORENZ_handleButtonEvent` (328),
  `LORENZ_handleEncoderEvent` (347), `LORENZ_isr` (178). Harrington 1200 follows
  the same shape (`H1200_storageSize` 1019, `H1200_save` 1023, `H1200_restore`
  1027, and the rest). Verified. The `static` linkage requires the App aggregate
  to be built in the same TU as the app header.

Low-rents:

- Settings count. `enum LORENZ_SETTINGS` runs `LORENZ_SETTING_FREQ1` through
  `LORENZ_SETTING_OUT_D`, so `LORENZ_SETTING_LAST = 10`. Confirmed 10 settings.
  Verified.
- Deps. Includes are `streams_lorenz_generator.h`, `util/util_math.h`,
  `OC_digital_inputs.h`. None transitively include `src/drivers/display.h`. The
  screensaver calls `OC::vectorscope_render` (`APP_LORENZ.h:290`), which the
  shim `OC_menus` provides. Verified.

Harrington 1200:

- Settings count. `H1200Settings : public settings::SettingsBase<H1200Settings,
  H1200_SETTING_LAST>` with 37 enumerators before `H1200_SETTING_LAST`. Confirmed
  37 settings. Verified.
- Dynamic menu. `update_enabled_settings()` (332), `enabled_settings_[]` (387),
  and `num_enabled_settings_` (385) implement conditional menu visibility over
  the fixed 37 slots. Verified. This is reflected in the scope, NT-param add-on,
  and Harrington 1200 entry sections; storage and the NT param table stay
  fixed-size.
- Deps. Includes are `OC_bitmaps.h`, `OC_strings.h`, `OC_trigger_delays.h`,
  `tonnetz/tonnetz_state.h`, `util/util_settings.h`, `util/util_ringbuffer.h`,
  `bjorklund.h`. None transitively include `src/drivers/display.h`.
  `OC_strings.cpp` is the only net-new non-header-only dep; it includes only
  `OC_strings.h` and `OC_version.h`, both of which include only `<stdint.h>`, so
  it compiles ARM-clean via the existing vendor_src rule. Euclidean is already a
  shim source (`shim/src/cv_map/bjorklund.cpp` with `shim/include/cv_map/
  bjorklund.h`, both in `SHIM_CORE_SRCS`), so Harrington 1200 reuses it rather
  than recompiling vendor `bjorklund.cpp`. Verified.

I/O accessor surface (traced against the Low-rents and Harrington 1200 call
sites):

- Templated accessors are required. Low-rents calls
  `OC::ADC::value<ADC_CHANNEL_1>()` (`APP_LORENZ.h:191`),
  `OC::DAC::set<DAC_CHANNEL_A>()` (238), and
  `OC::DigitalInputs::clocked<OC::DIGITAL_INPUT_1>()` (180). Vendor declares
  `ADC_CHANNEL`/`DAC_CHANNEL` as `using = int` with extern channel objects and
  the accessors take them by reference. The current shim `enum DAC_CHANNEL` is
  incompatible. Recorded in the I/O accessors section and Risks. Verified.
- Channel guard. Low-rents reads `ADC_CHANNEL_5..8` only under
  `#ifdef ARDUINO_TEENSY41` (`APP_LORENZ.h:185-194`); the `#else` reads
  `ADC_CHANNEL_1..4`. The shim must leave that macro undefined. Verified.
- Extra DAC and ADC methods. Harrington 1200 calls
  `OC::DAC::get_voltage_scaling` and `OC::DAC::set_voltage_scaled_semitone`
  (`APP_H1200.h:631-641`) and `OC::ADC::raw_pitch_value`
  (`APP_H1200.h:714-723`). Added to the shim DAC and ADC surface. Verified.
- DAC output history. `vectorscope_render` (Low-rents screensaver) reaches
  `OC::DAC::getHistory` via `scope_averaging` (`OC_menus.cpp:116`), so the shim
  DAC must keep a per-channel output-history ring and expose `getHistory` plus
  `kHistoryDepth`. `DAC_CHANNEL_LAST` is also an array bound
  (`averaged_scope_history`, `OC_menus.cpp:101`), so the representation change
  must retain it. Added to the DAC surface. Verified.
- `NT_setParameterFromUi` is available in the harness (`nt_runtime.h:28` hook,
  exercised in `test_host_Quadrants_host_proxy.cpp`), so the param-add-on
  app-side push links and is testable on host. Confirmed, no gap.

Param add-on and graphics surface:

- `settings::value_attr` exposes `default_`, `min_`, `max_`, `name`, and
  `value_names` (`util/util_settings.h:37-43`), so the NT parameter table
  generates from it directly (Low-rents row at `APP_LORENZ.h:153` carries enum
  labels such as `lorenz_output_names`). Verified.
- Enum-label index mismatch. The vendor menu indexes `value_names[value]`
  absolutely (`OC_menus.h:306`), but NT enum params index min-relative. The shim
  must pass `value_names + min`. Both validation apps use `min == 0` for every
  enum setting, so neither test exercises this; it is a latent rule for catalog
  apps with nonzero enum `min`. Recorded in the NT-param add-on section.
  Verified.
- The 12-method `shim::Graphics` extension covers the ported menu widgets.
  Vendor `OC_menus` calls `drawHLine`, `drawHLinePattern`, `print_right`, and
  `pretty_print_right` beyond the current shim set, all in the 12-method delta.
  The two validation apps plus the menu exercise 6 of the 12
  (`drawHLine`, `drawHLinePattern`, `movePrintPos`, `pretty_print`,
  `pretty_print_right`, `print_right`); the remaining 6 are added for the
  broader catalog under the full-surface scope. Verified.

Runtime and rendering surface:

- `UiControl` lives in `namespace OC` (`OC_ui.h:13,20`) and Low-rents compares
  `OC::CONTROL_ENCODER_R`. The shim `OC_ui.h` must put the enum in `namespace
  OC` with the default (non-NORTHERNLIGHT) bit assignment
  (`CONTROL_BUTTON_L = 1 << 2`, `CONTROL_BUTTON_R = 1 << 3`). Verified.
- Centering has no existing mechanism for direct `graphics.*` calls. The
  `gfx_offset` used by Hemisphere is applied in the `HemisphereApplet` draw
  wrappers (`HemisphereApplet.h:171`), not in `shim::Graphics`, whose `set_pixel`
  writes raw to the 256-wide `NT_screen` (`shim/src/graphics.cpp:12-17`). The
  spec now centers with a per-row byte-aligned shift in the runtime instead of
  altering shared graphics code. Verified.
- `FASTRUN` is not stubbed. `shim/include/Arduino.h` defines `DMAMEM`,
  `PROGMEM`, `FLASHMEM` but not `FASTRUN`, which Low-rents uses on `LORENZ_isr`.
  Added a `#define FASTRUN` requirement. Verified.
- Menu and math reachability. The menu API the apps call (`ScreenCursor::Init`,
  `AdjustEnd`, `Scroll`, `cursor_pos`, `editing`, `set_editing`,
  `toggle_editing`; `SettingsList`; `SettingsListItem`; `DefaultTitleBar`;
  `DualTitleBar`; `kScreenLines`; `kDefaultValueX`) matches the Menu widgets
  section. `SCALE8_16` and `USAT16` are header-only in `util/util_math.h`
  (164, 75-83), which Low-rents includes. Verified.
- Control-router coverage. Low-rents `handleButtonEvent` switches on
  `CONTROL_BUTTON_UP/DOWN/L/R` (on `EVENT_BUTTON_PRESS`, `APP_LORENZ.h:328`) and
  `handleEncoderEvent` on `CONTROL_ENCODER_L/R` (347). Harrington 1200 adds
  `EVENT_BUTTON_LONG_PRESS` on `CONTROL_BUTTON_L` (`APP_H1200.h:1088`). The
  router mapping (buttons 3 and 4 to UP and DOWN, encoder pushes to L and R,
  encoder deltas, plus long-press timing) covers both apps exactly. Neither app
  reads `event.mask`, so the chord feature is built for later apps (Meta-Q,
  Sequins) but is not exercised by the validation set. Verified.

Full external-symbol sweep of both apps:

- Every `OC::` symbol Low-rents references is covered (ADC/DAC/DigitalInputs,
  `AppEvent`, the `CONTROL_*` values, `vectorscope_render`). Verified.
- Harrington 1200 references three vendor symbols the spec did not originally
  cover: `OC::visualize_pitch_classes` (now in the menu hand-port);
  `OC::Strings::cv_input_names_none` / `trigger_delay_times` /
  `OC::trigger_delay_ticks` / `kNumDelayTimes` (now in the extended
  `OC_strings.h`); and `OC::kMaxTriggerDelayTicks` from `OC_config.h` (now a
  minimal shim `OC_config.h`). Recorded in the body. Verified.
- `OC::SemitoneQuantizer` resolves through the already-shimmed `OC_scales.h`
  (`OC_scales.h:41`); no new surface. `OC::TriggerDelays` is header-only
  (`OC_trigger_delays.h:33`) with shimmed or include-free deps. Verified.
- No Arduino free-function calls (`random`, `millis`, `micros`, `analogRead`,
  `map`, `delay`) appear in either app header. Verified.

Build wiring and harness:

- `BUILD_PER_APPLET` compiles one `.cpp`, partial-links `VENDOR_DEPS` plus
  `COMPILER_RT_OBJS`, removes `.group`, merges via `merge_sections.lds`, and
  strips. `BUILD_PER_OC_APP` mirrors it. The `build/arm/vendor_src/%.o` rule
  matches top-level vendor cpps such as `OC_strings.cpp`. Verified.
- Euclidean is already shim-owned. `shim/src/cv_map/bjorklund.cpp` and
  `shim/include/cv_map/bjorklund.h` exist and are in `SHIM_CORE_SRCS`, so
  Harrington 1200 reuses them; `OC_strings.cpp` is its only net-new vendor cpp.
  The earlier claim that it also recompiles vendor `bjorklund.cpp` was wrong.
  Corrected in the body. Link-safe: vendor `bjorklund.h` and shim
  `cv_map/bjorklund.h` declare the identical `bool EuclideanFilter(uint8_t,
  uint8_t, uint8_t, uint32_t)` (line 51 in both), so Harrington 1200's bare
  include (which resolves to vendor) links against the shim definition without
  signature mismatch. Verified.
- The harness drives `step`/`draw` (`main.cpp:326,344`) and `parameterChanged`
  (`nt_runtime.cpp:227`) but has no `customUi` call site and never builds a
  `_NT_uiData` (`plugin_loader.cpp:93-94` leaves the field null). The router
  tests need a net-new `_NT_uiData` synthesis driver. Recorded in the validation
  plan. Verified.
- ARM aggregation. `_per_applet_runtime.h` pulls `hem_shim_impl.h`, which
  `#include`s the shim `.cpp` bodies (`:4-14`) so each `.o` is one TU. The O_C
  runtime needs its own aggregation header (not the Hemisphere-coupled one).
  Verified.

Runtime time base and factory:

- `OC::CORE::ticks` is incremented only at `_per_applet_runtime.h:203`. A
  standalone O_C app does not run that path, so its runtime must increment
  `ticks` per isr tick or `millis`/`micros`/trigger-delays/screensaver-timeout
  freeze. Recorded in the I/O accessors and cadence sections. Verified.
- `_NT_factory` orders `tags` before `hasCustomUi`/`customUi` (`api.h:468`); the
  O_C factory must follow `aeabi_probe.cpp`'s field order. Recorded in Risks.
  Verified.

All traced facts match vendor source. Sixteen corrections or surface additions
surfaced across this verification and were folded back into the body above: the
false `DECLARE_APP` claim; the Harrington 1200 flat-settings mislabel; the
missing DAC and ADC methods; the `enum DAC_CHANNEL` to extern-object change; the
under-specified centering (now a per-row byte-aligned shift); the missing
`FASTRUN` stub; the missing `visualize_pitch_classes` in the menu hand-port; the
four extra `OC_strings.h` tables and constants Harrington 1200 needs; the
net-new minimal `OC_config.h` for `OC_CORE_ISR_FREQ` and `kMaxTriggerDelayTicks`;
the wrong claim that Harrington 1200 recompiles vendor `bjorklund.cpp` (it
reuses the shim source); the harness `customUi` driver gap; the O_C runtime's
own ARM aggregation header; the `OC::CORE::ticks` increment responsibility; the
`_NT_factory` field-order constraint; the enum-label index offset
(`value_names + min`) the param add-on must apply; and the DAC output-history
ring (`getHistory` / `kHistoryDepth`) the Low-rents screensaver needs.

### Shim prereq verification

- `shim/include/hem_graphics.h` exists with the `graphics` global and the base
  draw API. The 12 missing methods and the `weegfx` compat namespace are
  additive (no existing method changes). Confirmed against the current shim.
- `shim/include/OC_DAC.h`, `OC_core.h`, `OC_strings.h` exist as partial stubs.
  `OC_core.h` and `OC_strings.h` extend additively. `OC_DAC.h` is not purely
  additive: its `enum DAC_CHANNEL` must change to the vendor `using = int` plus
  extern-object representation, which touches all applet consumers (see Risks).
  `OC_ADC.h`, `OC_digital_inputs.h`, `OC_apps.h`, `OC_ui.h`, `OC_menus.h`,
  `OC_bitmaps.h` do not exist and are net-new. Confirmed against the current
  shim.
- `util/util_settings.h` and `UI/ui_events.h` compile unmodified (header-only,
  no display or driver dependency). Confirmed against vendor source.
- The `build/arm/vendor_src/%.o: $(HEM_SRC_DIR)/%.cpp` Makefile rule and the
  `-Ishim/include -I$(HEM_SRC_DIR)` ordering exist and are reused unchanged.
  Confirmed against the current Makefile.

## Implementation notes (foundation built 2026-05-28)

The foundation shipped on branch `dr/oc-apps-foundation` across Layer 0 (eight
shim tasks), Layer 0.5 (build skeleton plus a stub app), and Layer 1 (Low-rents
and Harrington 1200). Build is green: `make test`, `make test-applets` (55/55),
`make arm`. `.text` per `.o`: StubApp 8108 B, Low_rents 10992 B, Harrington1200
14848 B, all far under the ~82 KB cap. Several things diverged from this spec
during the build; they are recorded here so the per-app port issues that follow
do not re-derive them.

- customUI event emission lives in the per-app `.cpp`, not the runtime. Vendor
  `UI/ui_events.h` defines `Event` in top-level `::UI::`, while `OC_apps.h`
  forward-declares `OC::UI::Event`. The runtime owns the router state machine
  (edge detection, 500 ms long-press timing via `classify_release`, idle reset,
  `.mask` through `last_controls_of`) and exposes those primitives; the per-app
  `.cpp` constructs the `::UI::Event` and dispatches it, bridging the App
  handler pointers with a `reinterpret_cast` (layout-identical structs). See
  `plugins/apps/Low_rents.cpp` and `Harrington1200.cpp` for the canonical glue.
- Build model. A per-app TU defines `NT_OC_APP_TU`, which makes
  `_per_app_runtime.h` pull `shim/include/oc_shim_impl.h` (the OC aggregation
  header, guarded by the shared `NT_HEM_NO_IMPL` sentinel). `BUILD_PER_OC_APP`
  mirrors `BUILD_PER_APPLET`; the host `test_oc_app_%` rule links the app `.cpp`
  (which aggregates) and does not pull `SHIM_CORE_SRCS`. `.SECONDEXPANSION` plus
  `$$*` is needed so `VENDOR_DEP_HOST_SRCS_<APP>` binds in the pattern-rule
  prerequisite.
- Include-guard poison is the shadowing technique for a vendor header that is
  quote-included from inside another vendor header (so `-Ishim/include` cannot
  shadow it). The shim shadow defines the vendor header's own `_H_` include
  guard, so the vendor sibling self-suppresses when the shim is included first.
  Applied to `OC_digital_inputs.h`, `OC_bitmaps.h`, `OC_strings.h`, and
  `util/util_math.h`.
- Harrington 1200 links no net-new vendor `.cpp`. The spec planned to link
  vendor `OC_strings.cpp`, but that duplicates `note_names` /
  `note_names_unpadded` / `capital_letters`, which `globals.cpp` already defines
  (and which rides the OC aggregation). The three net-new tables
  (`cv_input_names_none`, `trigger_delay_times`, `trigger_delay_ticks`) were
  added to `globals.cpp` instead; `VENDOR_DEPS_Harrington1200` is empty.
- `parameterChanged` and the I/O routing accessors read the firmware-managed
  `alg.v` (not the private `v_storage` snapshot). The firmware repoints `v`
  after construct, so live param and routing edits only appear in `alg.v`.
- `DIGITAL_INPUT_1_MASK..4_MASK` were added to the shim `OC_digital_inputs.h`
  (the vendor `DIGITAL_INPUT_MASK(x)` macro lived in the now-suppressed vendor
  body). Harrington 1200 ANDs `clocked()` against these.

Deferred follow-ups (not blockers; track as issues):

- Relocate the OC string tables out of `globals.cpp` into an OC-only TU (the way
  `current_app` moved to `shim/src/oc/io.cpp`). Today `globals.cpp` defines six
  OC string symbols unconditionally, so a future app that needs vendor
  `OC_strings.cpp` would hit a duplicate-symbol link error. Update the stale
  `test_oc_strings` Makefile comment at the same time.
- ADC scale unification: the shim collapses vendor's separate `value()` (raw
  counts) and `pitch_value()` scales into the single 1536-per-octave NT bus
  space. Harrington 1200's CV-controlled knob math (`>> 9` / `>> 8`) will respond
  at a different per-step ratio than hardware; verify on the hardware smoke
  check and adjust per-app scaling if needed.
- Manifest `inputs[]` / `outputs[]` `BusParam` arrays are currently inert (the
  parameter table uses hardcoded "CV in 1".."TR in 4"). Either wire the
  descriptive names into `emit_io_params` or drop the arrays.
- Makefile host-test rules do not list headers as prerequisites, so editing a
  shim header does not auto-rebuild host test binaries. Add header deps or
  document the `rm -f` workaround.

## Validation coverage actually exercised

Both apps pass host Catch2 suites through the `nt_runtime` simulator and the
factory path: Low_rents 72 assertions / 6 cases, Harrington1200 193 assertions /
7 cases, plus the foundation suites (runtime 167/10, router 14/5, menus 37/10,
io 30/12, apps 18/4, strings 4/1, stub 32/4). Hardware smoke (Layer 3) is
deferred to after PR open since it needs physical access.
