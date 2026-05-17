# NT Deploy Mechanism (verified for firmware 1.13+)

Three paths from build to running plug-in, fast to slow.

## Preferred: `make deploy-sysex` (no reboot)

In-tree target. Sends `build/arm/Hemispheres.o` to the NT over USB MIDI SysEx and triggers a plug-in rescan. No USB disk mode, no module reboot, no menu navigation. Iteration cycle is roughly one second per build.

Requirements:

- NT firmware v1.13 or later. The rescan SysEx command was added in v1.13.
- Host Python with `mido` and `python-rtmidi` installed (`pip install mido python-rtmidi`).
- NT connected via USB and running normally (not in disk mode).

Usage:

```
make deploy-sysex                                   # default: Hemispheres.o, SysEx ID 0
make deploy-sysex SYSEX_PLUGIN=build/arm/gain.o     # override plug-in
make deploy-sysex SYSEX_ID=3                        # override SysEx ID
```

Under the hood: `harness/scripts/push_plugin_to_device.py` (vendored verbatim from `expertsleepersltd/distingNT@abe311cf`). The script writes the `.o` to the module's `programs/plug-ins/` and issues the rescan command. Optional third positional argument saves current state to a named preset, uploads, reloads.

Verify the upload via Misc menu, Plug-ins, View info. Each `.o` lists pass/fail and memory stats.

## Alternative: nt_helper

[nt_helper](https://github.com/thorinside/nt_helper) is a cross-platform Flutter GUI that does the same SysEx upload plus richer module inspection. Use when you want a UI, multi-file batch upload, or live parameter editing alongside the deploy.

There is also an `nt_helper` MCP server exposing the same operations as tools (`mcp__nt_helper__add`, `mcp__nt_helper__save`, `mcp__nt_helper__show_*`, etc). Use this from inside an agent session to inspect routing, parameters, or CPU while iterating.

## Fallback: USB MSC disk mode

Use when SysEx is unavailable (firmware older than v1.13, USB MIDI flaky, or first-time card setup).

1. Build: `make arm` produces `build/arm/*.o`.
2. On the NT, Misc menu, "Enter USB disk mode". The module reboots and the MicroSD card mounts as a removable drive on the host.
3. On macOS the volume appears at `/Volumes/<label>` where `<label>` is the SD card's volume label, or "NO NAME" if unlabeled. Check `ls /Volumes/`.
4. Deploy: `make deploy DEVICE=/Volumes/<label>`. The Makefile creates `programs/plug-ins/` if absent and copies `build/arm/*.o` into it.
5. Eject the volume on the host (drag to trash, or `diskutil eject /Volumes/<label>`).
6. On the NT, press both encoders together to reboot into normal mode.
7. Verify via Misc, Plug-ins, View info.

`DEVICE` defaults to `/Volumes/NT` in the Makefile. Override per invocation when the SD label differs.

## Quirks

- The NT is not USB-host capable. You cannot deploy via a USB stick plugged into the NT itself.
- The module does not draw power from USB. It must be powered from Eurorack while connected.
- USB disk mode suspends all audio and MIDI processing. Plug-ins cannot reload while audio runs.
- The SD card must be FAT32 (formatted via the SD Association tool is recommended).
- The "View info..." screen reports the API version each plug-in was built against. The harness builds against `kNT_apiVersion13`. Lower firmware API versions may disable some features.

## Source

- disting NT user manual v1.9, sections "Plug-ins" (page ~145) and "Enter USB disk mode" (page ~44). <https://www.expert-sleepers.co.uk/downloads/manuals/disting_NT_user_manual_1.9.pdf>
- Upstream SysEx tool: <https://github.com/expertsleepersltd/distingNT/blob/main/tools/push_plugin_to_device.py>

The v1.1 manual does not cover plug-ins. The C++ plug-in API was added in firmware 1.7.0. The SysEx rescan command used by `make deploy-sysex` was added in 1.13. Always cross-reference the manual matching the running firmware.
