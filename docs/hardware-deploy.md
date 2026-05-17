# NT Deploy Mechanism (verified for firmware 1.7+)

## Preferred: nt_helper plugin upload (no reboot)

[nt_helper](https://github.com/thorinside/nt_helper) is a cross-platform Flutter app that uploads `.o` plug-ins to the NT over USB MIDI SysEx (512-byte chunks) and auto-triggers a plug-in rescan. No USB disk mode, no module reboot, no menu navigation. Use this for the inner development loop.

Procedure:

1. Connect the NT via USB while running normally (no disk mode).
2. Open nt_helper. It enumerates the NT over USB MIDI.
3. Plugin Manager → upload local file → pick `build/arm/<name>.o`. Repeat for each plug-in or batch.
4. Wait a second; the module rescans automatically. Verify via Misc → Plug-ins → View info.
5. Add the plug-in in any preset slot.

Iteration cycle is ~1 second per build vs ~30 seconds for USB disk mode.

## Fallback: USB MSC via "USB disk mode"

## Source

disting NT user manual v1.9, section "Plug-ins" (page ~145) and "Enter USB disk mode" (page ~44). Manual download:
<https://www.expert-sleepers.co.uk/downloads/manuals/disting_NT_user_manual_1.9.pdf>

Note: the v1.1 manual does not cover plug-ins; the C++ API was added in firmware 1.7.0. Always cross-reference the manual matching the running firmware.

## Procedure

1. Build: `make arm` produces `build/arm/*.o`.
2. On the NT: Misc menu → "Enter USB disk mode". The module reboots into a state where the MicroSD card appears as a removable drive on the host.
3. On the host (macOS): the volume mounts at `/Volumes/<label>`. The default label appears to be the SD card's volume label; if no label, macOS uses "NO NAME". Check `ls /Volumes/` to discover.
4. Deploy: `make deploy DEVICE=/Volumes/<label>`. The Makefile creates `programs/plug-ins/` if absent, then copies `build/arm/*.o` into it.
5. Eject the volume on the host (drag to trash, or `diskutil eject /Volumes/<label>`).
6. On the NT: press both encoders together to reboot into normal mode.
7. Verify: Misc menu → "Plug-ins" → "View info..." lists each `.o` with a pass/fail flag and memory stats.

## Quirks

- The NT is not USB-host capable; you cannot deploy via a USB stick plugged into the NT itself.
- The module does not draw power from USB; it must be powered from Eurorack while connected.
- "USB disk mode" suspends all audio/MIDI processing. Plug-ins cannot be reloaded while audio is running.
- The card must be FAT32 (formatted via the SD Association tool is recommended).

## Notes for the harness

- `DEVICE` defaults to `/Volumes/NT` in the Makefile. The actual mount label is whatever the SD card was formatted with. Override per invocation: `make deploy DEVICE=/Volumes/<actual_label>`.
- The plug-in scan happens at module startup (after USB disk mode exit) and when the card is remounted via Misc menu. No hot-reload while running.
- The "View info..." screen reports the API version each plug-in was built against. The harness builds against `kNT_apiVersion13` (current). If the module's firmware reports a lower API version, some features may be unavailable.
