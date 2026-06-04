# Epic #72 Batch 4: NT MIDI I/O bridge + hMIDIIn / hMIDIOut (design)

Vendor pin: `7800d929`. Audit: #71. Epic: #72. This is the one real subsystem
in the epic.

## Problem

hMIDIIn and hMIDIOut read and write `HS::frame.MIDIState` (a vendor `MIDIFrame`).
The shim has NO MIDI infrastructure: no `MIDIFrame`, no `IOFrame::MIDIState`, and
the vendor feed (`ProcessMIDI` reading Teensy usbMIDI) does not exist on the NT.
The NT firmware instead delivers MIDI through `_NT_factory` callbacks
`midiMessage(self,b0,b1,b2)` / `midiRealtime(self,byte)` and sends through
`NT_sendMidi2ByteMessage` / `NT_sendMidi3ByteMessage`
(`vendor/distingNT_API/include/distingnt/api.h:457-466,615-622`).

## Branch base

Branched from `dr/epic72-batch1` to inherit `HS::trig_length` (added there;
`MIDIMapping::ClockOut` needs it). Merge after #73.

## Layer 0a: shim MIDI state (parent-owned shared infrastructure)

Port the vendor `MIDIFrame` and its dependent types into a NEW shim header
`shim/include/HSMIDIFrame.h` plus impl in `shim/src/midi_frame.cpp`. The ported
struct is shim-owned (not vendor), so the port MAY adapt for the NT (no heap, no
Teensy). Required types (vendor `HSIOFrame.h:34-487` + `HSIOFrame.cpp`):

- `MIDIMessage` (channel 1-based; `chan()`, `note()`, `vel()`, `IsNote()`),
  `using MIDILogEntry = MIDIMessage`.
- `MIDINoteData { uint8_t note, vel; }`, `PolyphonyData { uint8_t note, vel;
  bool gate; }`.
- `MIDIMapSettings` + `MIDIMapping` (verbatim fields and methods: `IsClock`,
  `IsTrigger`, `clock_mod`, `ClockOut`, `ProcessClock`, `InRange`, Adjust*
  setters, `AutoLearn`, `Pack`/`Unpack`). `ClockOut` uses `HEMISPHERE_CLOCK_TICKS
  * HS::trig_length` (both shim-available after the batch-1 base).
- ADAPTATION (no heap): replace `using NoteBuffer = std::vector<MIDINoteData>`
  with a fixed-capacity inline container providing the EXACT subset used:
  default ctor, `size()`, `operator[]`, `push_back`, `clear`, `begin`/`end`,
  and an erase-by-value path (for `RemoveNoteData`). Capacity 16
  (`kNoteBufferCap = 16`). This removes any heap/arena dependency on the MIDI
  path; do NOT use the operator-new arena for note buffers.
- `MIDIFrame` struct: ALL fields from `HSIOFrame.h:163-...` (mapping
  `[MIDIMAP_MAX]`, outmap `[ADC_CHANNEL_COUNT]`, `note_buffer[16]`,
  `last_midi_channel`, `sustain_latch`, `pc_channel`, `poly_buffer
  [DAC_CHANNEL_COUNT]`, `max_voice`, `poly_mode`, `poly_rotate_index`,
  `midi_channel_filter`, `any_channel_omni`, clock flags, `clock_count`,
  `last_msg_tick`, output-side scratch fields, `log[7]`, `log_index`) and ALL
  methods listed in the surface report (Init, channel-filter, polyphony,
  mono/poly buffer push/pop, note getters, sustain, log, `ProcessMIDIMsg`,
  `Send`, get_in/out accessors).
- `ProcessMIDIMsg(MIDIMessage)`: port the verbatim `HSIOFrame.cpp` body
  (reproduced in the dispatch prompt). Decodes realtime (Clock/Start/Stop) and
  channel-voice (NoteOn/Off, CC, AfterTouch, PitchBend) into the buffers and
  `mapping[].output`. Replace `usbMIDI.Clock`/`.NoteOn`/... with the shim
  `HS::HemMidiType` / raw status nibbles. `MIDI_CLOCK_PPQN`, `MIDIQuantizer::CV`,
  `Proportion`, `HEMISPHERE_MAX_CV`, `HEMISPHERE_3V_CV`, `PULSE_VOLTAGE` are all
  shim-available.

New constants: `MIDIMAP_MAX = 8` (non-Teensy path), `HEMISPHERE_CHANGE_THRESHOLD`
`#define 32`.

## Layer 0b: send side -> NT MIDI

The ported `MIDIFrame::Send*` methods must emit real MIDI. Map each to the NT
API (ARM; host = harness stubs that record):

- `SendNoteOn(ch,note,vel)` -> `NT_sendMidi3ByteMessage(dest, 0x90|ch, note, vel)`
- `SendNoteOff(ch,note,vel)` -> `NT_sendMidi3ByteMessage(dest, 0x80|ch, note, vel)`
- `SendCC(ch,cc,val)` -> `NT_sendMidi3ByteMessage(dest, 0xB0|ch, cc, val)`
- `SendAfterTouch(ch,val)` -> `NT_sendMidi2ByteMessage(dest, 0xD0|ch, val)`
- `SendPitchBend(ch,bend14)` -> `NT_sendMidi3ByteMessage(dest, 0xE0|ch, bend&0x7F,
  (bend>>7)&0x7F)`

`dest` is the `_NT_midiDestination` bitmask (`api.h:109`); read it and pick the
standard output destination, document the choice.

## Layer 0c: receive wiring (factory ABI extension)

- Add `midiMessage` and `midiRealtime` to the `_NT_factory` initializer in the
  per-applet plug-in template. MIDI-consuming applets set:
  - `midiMessage_impl(self,b0,b1,b2)`: `MIDIMessage{ (b0&0x0F)+1, b0&0xF0, b1,
    b2 }` -> `frame.MIDIState.ProcessMIDIMsg(...)`.
  - `midiRealtime_impl(self,byte)`: `MIDIMessage{ 1, byte, 0, 0 }` ->
    `ProcessMIDIMsg`.
- Shared helpers `per_applet_runtime::route_midi_message` /
  `route_midi_realtime`. Non-MIDI applets leave the fields null.
- `frame.MIDIState` is per-TU; hMIDIIn populates and reads it in the same TU, so
  the per-TU-globals constraint holds.

## Layer 0d: harness MIDI simulator

- Host stubs for `NT_sendMidi2ByteMessage`/`NT_sendMidi3ByteMessage`/
  `NT_sendMidiByte` recording into a capturable buffer (`nt::midi_sent()`,
  `nt::clear_midi_sent()`).
- Inject seam `nt::send_midi_to_plugin(loaded,b0,b1,b2)` /
  `nt::send_midi_realtime(loaded,byte)` calling the factory callbacks, gated on
  `NT_HEM_HOST_SIM` (do not invent a parallel mechanism).

## Layer 1: the two applets (parallel after Layer 0)

- hMIDIIn (token `hMIDIIn`, GUID `HmMi`): MIDI->CV. No CV inputs; 2 outputs
  (mapped function + clock-run gate). Pack: `map_index[0]`@[0,5),
  `map_index[1]`@[8,5). Icons: `PhzIcons::midiIn`, `MIDI_ICON`,
  `PhzIcons::keyboard`, `PhzIcons::filter`.
- hMIDIOut (token `hMIDIOut`, GUID `HmMo`): CV->MIDI. Digital-in Gate, CV-in
  Pitch + selectable function. `hMIDI.Send*` -> NT MIDI;
  `MIDIQuantizer::NoteNumber`. Pack: channel@[0,4), legato@[7,1),
  functionA@[8,8), functionB@[16,8), transpose@[24,8). Icons:
  `PhzIcons::midiOut`, `MIDI_ICON`, `NOTE_ICON`, `MOD_ICON`, `AFTERTOUCH_ICON`,
  `BEND_ICON`, `CHECK_ON/OFF_ICON`, `RIGHT_ICON`.

Both standalone; `IsLinked` only matters in a composer host.

## Coverage

- hMIDIIn: inject NoteOn via the harness seam, step, assert the mapped output CV
  == `MIDIQuantizer::CV(note)`; NoteOff drops gate/output; inject MIDI clock
  realtime, assert clock_run / clock-out. Round-trip the pack. SHAPE 2 where
  clocked.
- hMIDIOut: drive Gate + Pitch CV, step, assert `nt::midi_sent()` contains
  NoteOn(channel, `MIDIQuantizer::NoteNumber(cv)`, vel); release gate -> NoteOff.
  Round-trip the pack.

## Spec footer

### Recipe spot-check

Per-applet recipe unchanged for the two applets; the novelty is the Layer-0
shim MIDI subsystem, a faithful `MIDIFrame` port with two adaptations
(fixed-capacity NoteBuffer; send -> `NT_sendMidi*`) plus receive wiring through
the NT factory callbacks.

### Per-entry verification (traced against vendor `7800d929`)

- hMIDIIn (`applets/hMIDIIn.h:26`): reads `frame.MIDIState.mapping[].output`,
  `.clock_run`, calls `UpdateMidiChannelFilter`/`UpdateMaxPolyphony`/buffer
  clears; OnDataRequest packs only `map_index[0/1]`. CONSISTENT.
- hMIDIOut (`applets/hMIDIOut.h:25`): `auto &hMIDI = HS::frame.MIDIState`;
  `SendNoteOn/Off`, `SendCC`, `SendAfterTouch`, `SendPitchBend`;
  `MIDIQuantizer::NoteNumber(In(0),transpose)`; writes `outmap[]`. CONSISTENT.
- `ProcessMIDIMsg` decode (vendor `HSIOFrame.cpp`): the verbatim body is the
  authority; the port must match it message-type for message-type.

### Shim prereq verification

Present: `MIDIQuantizer` (real), the `HEM_MIDI_*`/`MIDIPolyMode` enums,
`MIDIQuantizer::CV`, `Proportion`, `HEMISPHERE_MAX_CV/3V_CV`, `PULSE_VOLTAGE`,
`MIDI_CLOCK_PPQN`, `HS::trig_length` (batch-1 base), `SlewedValue`,
`ADC_CHANNEL_COUNT`, `DAC_CHANNEL_COUNT`. Absent (this batch adds): the whole
`MIDIFrame` + `IOFrame::MIDIState`, `ProcessMIDIMsg`/`Send` bodies, the NT send
mapping, the factory `midiMessage`/`midiRealtime` wiring, the harness MIDI sim,
`MIDIMAP_MAX`, `HEMISPHERE_CHANGE_THRESHOLD`, the MIDI icons.

### Hardware verification caveat

The host harness models MIDI via stubs; it does NOT prove real MIDI on device.
hMIDIIn (firmware -> `midiMessage`) and hMIDIOut (`NT_sendMidi*` -> a port) are
hardware-only behaviors. A hardware MIDI smoke check (external keyboard ->
hMIDIIn -> CV read on Verifier; hMIDIOut -> a MIDI monitor) is a required
post-merge follow-up, not coverable by `make test-applets`.
