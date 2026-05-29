# Verifier probe design

Date: 2026-05-28
Issue: danryan/nt_and_crime#37
Vendor SHA (O_C-Phazerville): 7800d929 (not consumed by this work; Verifier is bespoke, no vendor applet)
NT API: vendor/distingNT_API (bus float ABI, NT_drawShapeI, NT_screen)

## Purpose

Verifier is an on-device disting NT measurement plug-in. It reads selected
input buses and renders a deterministic, screenshot-parseable readout to the
256x64 display. A host screenshots it over SysEx and parses the layout to
assert on measured values.

It exists to close the hardware-in-the-loop verification loop for
autonomously-created NT plug-ins. The module has no SysEx message that reads a
raw bus voltage (confirmed in issue #37). Verifier manufactures that readback
channel: it computes the value on-device and prints it where a parser can
recover it exactly. Command the preset, screenshot, parse, assert. No human
reads the screen.

This is the screen-readback instrument. The audio-interface instrument (Tier 2,
issue #35, via ES-9) is the higher-fidelity sibling for the residue Verifier
cannot reach.

## Verification topologies

Verifier reads any bus in the shared frame bus array, not only physical input
buses. Two topologies, primary first.

Primary, in-preset direct read (no cable):

- Load the plug-in-under-test and Verifier in the same preset, with Verifier
  AFTER the under-test in slot order.
- In a single frame the under-test writes its output bus, then Verifier reads
  that same bus from the shared array. Verifier reads the computed output
  directly, with no physical patch and no human.
- This verifies the algorithm logic (did it compute the right value), which is
  the goal of autonomous app verification. It is the fast, cable-free,
  sim-testable path.
- Slot order is load-bearing: Verifier must run after the under-test, or it
  reads the bus before the under-test has written it.

Secondary, physical loopback (cable):

- Patch an under-test output jack to a Verifier input jack.
- This additionally exercises the DAC-to-ADC analog path, a hardware-calibration
  concern separate from algorithm logic. Higher effort, needs a human to patch.
- Deferred to the hardware session.

## Why a bespoke plug-in over stock instruments

The module ships instruments that already render audio-rate measurements to the
display: `oscs` (oscilloscope), `tuns`/`tunf` (tuner), `cali` (auto-calibrator).
A purpose-built readout goes one step further: it controls the render format, so
the host parser reads a layout it was designed against instead of fighting an
arbitrary vendor rendering. That control recovers tight precision (print the
measured voltage as exact digits) and removes the per-session calibration that
issue #37 needed only because it read arbitrary bar heights.

## Scope

In scope this session (pure logic, no hardware):

- The Verifier `_NT_algorithm` plug-in: parameters, step measurement math,
  draw for both views. C++, built via the shim like existing probes.
- Catch2 host tests against the nt_runtime sim: measurement math and rendered
  pixels.
- Host-side python parser: numeric and scope, with pytest unit tests against
  synthetic fixtures.

Deferred to a hardware session:

- Real fixture capture from the device.
- The loopback assertion (plug-in-under-test output to Verifier input).
- Device ADC/DAC tolerance characterization.

Out of scope (Tier 2, issue #35):

- Sample-accurate timing or phase between two signals.
- Raw sample-stream capture.
- Any quantity with no on-device readout.

## The plug-in

Bespoke `_NT_algorithm` under `plugins/probes/Verifier.cpp`, following the
`plugins/probes/bus_probe.cpp` recipe exactly: a struct extending
`_NT_algorithm`, a `parameters[]` table, parameter pages,
`calculateRequirements`, `construct`, `parameterChanged`, `step`, `draw`, a
`_NT_factory`, and `pluginEntry`.

GUID: `NT_MULTICHAR('V','r','f','y')` ("Vrfy"). Name: "Verifier".

### Parameters (all SysEx-settable, the autonomy property)

The harness drives every behavior over SysEx `edit_parameter`. No UI poking is
required, which is the property that makes autonomous verification possible.

| Param | Type | Purpose |
| --- | --- | --- |
| View | enum {Numeric, Scope} | Selects the active screen |
| First bus | int 1..kNT_lastBus | First bus of the read subset (any bus, not only inputs) |
| Count | int 1..6 | Number of consecutive buses to read |
| Numeric mode | enum {Mean, Min, Max, PkPk} | Reduction shown in the numeric view |
| Reset | enum {Off, Reset} | Pulsing to Reset clears all accumulators |
| Scope bus | int 1..kNT_lastBus | Bus traced in the scope view |
| Timebase | int (samples/pixel) | Scope horizontal scale |

`edit_parameter` over the nt_helper MCP rejects numeric values (CLAUDE.md), so
the harness sets parameters over direct SysEx in the python path, not the MCP
tool. Enum params are settable either way.

### step

`step(self, busFrames, numFramesBy4)`; `numFrames = numFramesBy4 * 4`. Input
bus `b` is `busFrames + (b - 1) * numFrames`, a float array. The disting NT bus
ABI is in volts (1.0f equals 1V); bus_probe writes `level%/100` directly as the
bus value, confirming the volt interpretation. This is recorded as an
assumption to verify on hardware (see Hazards).

Per selected bus, accumulate across step calls since the last Reset. `draw`
does not reset; only pulsing the Reset parameter clears accumulators. This makes
a reading independent of screenshot timing: the harness pulses Reset, runs the
stimulus, then screenshots whenever convenient and reads a latched value.

The clear happens once, in `parameterChanged`, on the transition into Reset.
`parameterChanged` only fires on a value change, so the harness must drive an
Off-to-Reset edge (set Off, then Reset) to clear. Holding the parameter at
Reset does not re-clear; the clear already happened on the edge. `step` does not
read the Reset parameter.

- running sum (in a `double` to avoid float32 drift over long windows) and
  sample count for Mean (= sum / count since Reset),
- running min and max for Min, Max, and PkPk (= max minus min), latched since
  Reset (peak hold), so a transient peak between screenshots is still captured.

Push the scope bus into a fixed 256-sample buffer, one sample per pixel column,
decimating by the Timebase (keep every Timebase-th sample). The buffer is a
fixed 256 floats regardless of Timebase, so SRAM does not grow with the time
window. This bounds the struct size (see the SRAM-size hazard).

Verifier does not write any bus. It is read-only on the audio graph.

### draw

`draw` renders the active view to `NT_screen`. `NT_screen` is `128*64` bytes,
4 bits per pixel, two horizontal pixels per byte, representing 256x64. The
firmware unpacks it to 16384 one-byte pixels in the SysEx screenshot reply, so
the parser works on a 256x64 grayscale array (0..15).

Numeric view layout (fixed, parser-designed):

- One row per selected bus, top to bottom, row height 10 px (fits 6 rows in 64).
- Each row: bus label at a fixed x, then the signed voltage formatted `sNN.fff`
  (sign, two integer digits, decimal point, three fractional digits), one glyph
  per fixed 6 px column slot using Verifier's own 6x8 bitmap font (see Font).
  Seven glyph cells, fixed positions.
- Leading zeros are printed, never trimmed, so every glyph cell stays at a fixed
  x for the parser (for example `+01.000`, `-00.250`).
- Two integer digits cover the full NT bus swing (+-10 V and beyond, up to
  +-99.999 V). Clamp and flag overflow with a sentinel glyph if exceeded.
- Resolution is 1 mV, set by the printed digits, not by display coarseness.

Formatting without `snprintf`: newlib-nano omits `snprintf`/`vsnprintf`
(CLAUDE.md). Convert the reduction to integer millivolts (round
`value * 1000`), take the sign, then extract each decimal digit by repeated
division and render it. This follows the manual integer-formatting precedent in
`shim/src/graphics.cpp` and `host_proxy.cpp::format_u2`. `NT_intToString` is
available for whole-number labels but does not produce the fixed-width
fractional layout, so the digit extraction is hand-rolled.

The parser recovers each digit by template-matching Verifier's own font glyph at
its known `(x, y)` cell. Because Verifier defines the font and draws it pixel by
pixel, the bitmap is identical in the host sim and on hardware, and the template
match is unambiguous.

Scope view layout:

- A single trace for the Scope bus across the 256 px width.
- Per column x, map the buffered sample to y by a fixed voltage-to-pixel scale
  (for example +-5 V across the 64 px height, 0 V at row 32).
- Connect consecutive samples with `NT_drawShapeI` line segments.
- Trigger on the first rising zero-crossing in the buffer so a periodic shape
  renders stably frame to frame. If no rising zero-crossing exists (DC or
  silence), fall back to rendering untriggered from the buffer start, so a flat
  or non-periodic signal still draws a well-defined trace.

### Font decision

Verifier draws its own fixed 6x8 bitmap font, not the firmware font through
`NT_drawText`. The glyph table (digits `0`-`9`, `+`, `-`, `.`, and a `#`
overflow sentinel) lives in `verifier_logic.h`; each glyph is a 5-wide pattern
in a 6 px cell, 7 rows tall in an 8 px cell. `draw_glyph` lights each set pixel
via `NT_drawShapeI(kNT_point, x, y, x, y)`.

Why not `NT_drawText`:

- The host sim's `NT_drawText` font (`harness/src/font_placeholder.cpp`) is a
  placeholder: every glyph is an identical solid block. In-sim digit recovery
  cannot be tested against it.
- The firmware's own font bitmaps are opaque to the host parser, so an
  `NT_drawText` readout could never be hardware-valid without a separate
  font-capture step on hardware.

A self-drawn font removes both problems: the glyph bitmaps are identical in the
sim and on hardware (Verifier writes the pixels itself), so the parser templates
are repo-defined and hardware-valid immediately, with no deferred font
confirmation.

Nibble order: Verifier never hand-packs `NT_screen`. It draws through
`NT_drawShapeI`, whose `set_pixel` is supplied by the active backend (the sim in
tests, the firmware on hardware), so pixels land correctly on both despite the
sim and shim packing opposite nibbles. The parser is immune regardless: it reads
the firmware-unpacked 16384-byte screenshot, one byte per pixel.

## Host parser (python)

New module `harness/verifier/parser.py` (first python package under harness/).
Pure functions over a 256x64 grayscale array (the `nt_screenshot.capture`
output, or a fixture).

- `parse_numeric(screen, layout) -> dict[int, float]`: for each row, read the
  glyph cells at known positions, template-match Verifier's own font digit
  bitmaps, assemble the signed voltage. Returns bus -> volts.
- `parse_scope(screen, region, sample_rate, timebase) -> ScopeResult`:
  per-column lit-y to a sample series; derive a coarse shape (`flat`, `square`,
  or `wave`; sine, saw, and triangle all classify as `wave` at this resolution)
  and a frequency estimate. Frequency = `sample_rate / (period_px * timebase)`,
  where `period_px` is the zero-cross spacing in pixels; the screenshot does not
  carry the sample rate or timebase, so the harness passes them (it set the
  timebase and knows the rate).

The font digit bitmaps are Verifier's own glyph table, the single source of
truth in `verifier_logic.h`. A small C++ dump tool emits them to
`harness/verifier/fixtures/font.json`, which `harness/verifier/font.py` loads as
the parser's templates. Because Verifier draws exactly these bitmaps on both the
sim and hardware, the templates are hardware-valid with no deferred confirmation
step.

Transport: the harness captures via `harness/scripts/nt_screenshot.py`
(`capture()`, direct SysEx), not the `show_screen` MCP tool. The MCP tool is
interactive; direct SysEx runs headless in pytest.

## Tests

TDD throughout (RED before GREEN). Two suites.

C++ (Catch2, nt_runtime sim), new `harness/tests/test_verifier.cpp`:

- Measurement math: inject known per-bus sample windows, run `step`, assert
  Mean, Min, Max, PkPk against hand-computed expectations. Writing the bus value
  into `busFrames` before Verifier's `step` models the upstream plug-in-under-
  test, so the in-preset direct-read topology is fully sim-testable without a
  multi-slot sim. Assert that a Reset edge clears the accumulators.
- Numeric render: set known voltages, run `draw`, assert glyph cells are lit and
  that two values differing in one digit produce a different lit pattern in that
  digit's cell (value-dependence). Verifier draws via `NT_drawShapeI(kNT_point)`,
  so the sim/shim opposite-nibble packing (see
  [[project_nt_runtime_nibble_order]]) does not bite: the pixel-read helper uses
  the sim's own convention, matching what the sim's `set_pixel` wrote.
- Scope render: inject a known periodic buffer, run `draw`, assert the trace
  lit-pixel column profile matches the expected mapping.

A new Makefile host-test rule is needed (probes currently have no host-test
rule). Mirror `test_applet_%`. The plan settles the exact rule.

python (pytest), new `harness/verifier/tests/test_parser.py`:

- `parse_numeric` recovers known voltages from synthetic fixtures built by
  stamping Verifier's own font glyphs (from font.json) at the layout positions.
  Faithful because the layout and font are the ones Verifier renders.
- `parse_scope` recovers shape and frequency from synthetic traces.

pytest is added as a dev dependency (first python test suite in the repo).
Synthetic fixtures live under `harness/verifier/tests/fixtures/`; real fixtures
join them in the hardware session.

## Build wiring

- `plugins/probes/Verifier.cpp` gets an explicit ARM build rule
  `build/arm/Verifier.o: plugins/probes/Verifier.cpp` and is added to the `arm:`
  target, mirroring `build/arm/bus_probe.o`.
- A host-test rule builds `build/host/test_verifier` against `HARNESS_SRCS` and
  the nt_runtime sim. The plan settles whether to add a generic `test_probe_%`
  rule or a one-off.
- python: add `pytest` to a dev requirements file; the parser package is plain
  modules under `harness/verifier/`.

## Hazards checklist (from CLAUDE.md)

- 10x clocked multiplier: not applicable. Verifier reads no `Clock`; it does
  arithmetic over the raw sample window.
- numParameters must match the populated table range. Verifier has a static
  parameter table; `numParameters = ARRAY_SIZE(parameters)`. No phantom entries.
- Construct-time `parameterChanged`: Verifier's `parameterChanged` only updates
  cached param copies. It never calls `NT_setParameterFromUi`, so the
  construct-time re-entrancy hazard does not apply.
- serialise: Verifier holds no persisted state (measurement is live), so no
  `serialise`/`deserialise`. If state is added later, pack via
  `_NT_jsonStream::addNumber`, never `addString` (see
  [[project_oc_serialise_addnumber]]).
- .text budget (~82 KB per .o): Verifier is small (kin to bus_probe). No risk.
- SRAM-size cache: the struct holds the fixed 256-float scope buffer plus
  per-bus accumulators. Size is fixed (does not grow with Timebase or window).
  Firmware caches `calculateRequirements` at scan time, so any later struct
  growth needs a power cycle on the device (CLAUDE.md SRAM-size hazard).
- Bus-to-volts assumption: 1.0f equals 1V. Verify on hardware by feeding a known
  DC level through bus_probe and confirming the printed value. If the scale
  differs, the on-device conversion is a single constant.

## Spec footer

Recipe spot-check:

- Verifier mirrors `plugins/probes/bus_probe.cpp` structurally
  (struct/params/pages/calculateRequirements/construct/parameterChanged/step/
  draw/factory/pluginEntry). Confirmed against the file in this repo.
- `NT_screen`, `NT_drawShapeI` (with `kNT_point`) confirmed in
  `vendor/distingNT_API/include/distingnt/api.h`.

Per-entry verification (three readings traced end to end):

1. Bus reads a constant +1.000 V window. Mean = 1.000. Numeric view prints
   `+01.000` in the bus row. Parser template-matches `+`, `0`, `1`, `.`, `0`,
   `0`, `0` and returns `{bus: 1.000}`. Assertion within tolerance passes.
2. Bus reads a window with min -0.250 and max +0.750. Numeric mode PkPk: prints
   `+01.000`. Numeric mode Min: prints `-00.250`. Two parser reads, two values.
3. Scope bus reads a 1 kHz triangle. Trace renders a triangle profile; shape
   classifier returns triangle; zero-cross spacing yields ~1 kHz within the
   timebase resolution.

If more than one of these three contradicts the implemented behavior during
build, audit the layout and measurement math before proceeding.

Shim prereq verification:

- No new shim surface is required. Verifier uses only the NT API
  (`NT_screen`, `NT_drawShapeI`) and the bus float ABI, all already present.
  Confirmed: no `shim/include` additions, no icon stubs, no manifest (Verifier
  is not an applet or O_C app).

## Canonical recipe recap (adding a similar instrument later)

1. Create `plugins/probes/<Name>.cpp` from the bus_probe recipe.
2. Define parameters covering View and the read subset; keep every behavior
   parameter-driven for SysEx control.
3. `step` reads input buses (volts) and reduces over the window; no bus writes.
4. `draw` renders a fixed, parser-designed layout with a self-drawn bitmap font
   via `NT_drawShapeI(kNT_point)`.
5. Add the ARM build rule and the `arm:` target slot.
6. Add a Catch2 host test (measurement math plus rendered pixels) and a python
   parser with pytest fixtures.
7. Verify on hardware: known DC in, parsed value out, loopback assertion.
