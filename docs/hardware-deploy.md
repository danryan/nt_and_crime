# NT Deploy Mechanism (verified for firmware 1.7+)

## Mechanism

USB MSC via the MicroSD card slot. The NT exposes its MicroSD as a removable drive while in "USB disk mode". Plug-ins live at `programs/plug-ins/` on the card. File extension: `.o`.

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
