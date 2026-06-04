# Epic #72 Batch 4 plan: NT MIDI I/O bridge + hMIDIIn / hMIDIOut

Spec: `docs/superpowers/specs/2026-06-04-epic72-batch4-midi-bridge-design.md`.
Audit: #71. Epic: #72. Vendor pin `7800d929`.

## DAG

- Layer 0 (shim MIDI subsystem, parent-owned): `HSMIDIFrame.h` +
  `midi_frame.cpp` (MIDIFrame port, heap-free NoteBuffer, ProcessMIDIMsg/Send),
  `IOFrame::MIDIState`, the NT send mapping, the factory `midiMessage`/
  `midiRealtime` receive wiring in `_per_applet_runtime.h`, and the harness MIDI
  sim. This is the bulk; built as a cohesive unit (commit `9298f9d`).
- Layer 1: hMIDIIn (`HmMi`) + hMIDIOut (`HmMo`) applets (commit `f14fe08`).
- Layer 2 (verification): full `make test-applets` regression green (69
  binaries); both MIDI applet host tests green (hMIDIIn 37 / hMIDIOut 33); both
  ARM `.o` clean (~29 KB .text) with `NT_sendMidi*` as the only new
  firmware-resolved symbols and no heap/`std::vector` symbols.
- Layer 3 (hardware, post-PR): real MIDI is hardware-only. hMIDIIn needs the
  firmware to deliver MIDI to `midiMessage` (external keyboard -> CV read on the
  Verifier); hMIDIOut needs `NT_sendMidi*` to reach a port (MIDI monitor). The
  host harness stubs these; they do NOT prove on-device MIDI.

## Branch base / stacking

Branched from `dr/epic72-batch1` for `HS::trig_length`. The batch-4 PR diff
includes batch-1 commits until #73 merges; merge order #73 then this PR.

## Single cohesive implementer

The subsystem is tightly coupled (the MIDIFrame port, the receive wiring, the
send mapping, and the harness sim all reference each other), so it was built by
one implementer rather than fanned out. The two applets are thin consumers of
the bridge and were authored in the same pass.

## Design decisions recorded

- NoteBuffer: fixed-capacity-16 inline container, not `std::vector` -- keeps the
  MIDI path off the heap entirely (no operator-new/arena dependency).
- Send destination: `kNT_destinationBreakout | kNT_destinationUSB` (the physical
  MIDI outputs).
- apiVersion: unchanged. MIDI handling is `kNT_apiVersion3`, already covered by
  `kNT_apiVersionCurrent`; only the factory wiring was needed.
- Factory field order: `midiRealtime`/`midiMessage` placed between `draw` and
  `hasCustomUi` per `distingnt/api.h` (designated-initializer order matters).
- `HSMIDIFrame.h` includes vendor `PackingUtils.h` by BARE name (matching the
  other applets) so its `#pragma once` dedups against the vendor copy.

## Abort budget

- Layer 0: an ABI mismatch or heap dependency that cannot be removed after
  objdump/nm diagnosis -> BLOCKED with the specific blocker (not a rushed
  approximation). Did not fire.
- Verification: any regression on a shipped applet -> halt. Did not fire.
