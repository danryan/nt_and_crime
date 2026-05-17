# Disting NT SysEx Deploy Protocol

Notes for implementing `make deploy-sysex`. Distilled from `github.com/thorinside/nt_helper` source and its `docs/SYSEX_REFERENCE.md`.

## Message framing

All messages share a common header and footer:

```
F0 00 21 27 6D <sysExId> <command> [data...] F7
```

| Byte | Meaning |
|------|---------|
| `F0` | start of SysEx |
| `00 21 27` | Expert Sleepers manufacturer ID |
| `6D` | Disting NT device ID |
| `sysExId` | 0..126, per-device. Defaults to 0. |
| `command` | per-operation opcode |
| `F7` | end of SysEx |

## Operations used by deploy

| Opcode | Operation | Direction | Notes |
|--------|-----------|-----------|-------|
| `56` | Get current preset path | Host -> Device | Response carries ASCII path |
| `35` | New blank preset | Host -> Device | Unloads plug-in instances so the `.o` can be replaced |
| `34 00 <path> 00` | Load preset by path | Host -> Device | Restores the saved working state |
| `47 <name> 00` | Set preset name | Host -> Device | Used before `Save` to pin a temp name |
| `36 02` | Save preset | Host -> Device | No ACK; sleep ~500 ms before next op |
| `7A 04` | File upload chunk | Host -> Device | ACK per chunk |
| `7A 08` | Rescan plug-ins | Host -> Device | Requires checksum. Replaces full reboot on firmware v1.13+ |
| `7A 00 04` | File upload ACK | Device -> Host | Wait for this between chunks |
| `7F` | Reboot | Host -> Device | Only needed on pre-v1.13 firmware |

## File upload chunk format (`7A 04`)

```
F0 00 21 27 6D <id> 7A 04
  <path bytes...> 00              ; null-terminated destination path
  <createAlways: 1 byte>          ; 1 for first chunk, 0 for subsequent
  <position: 10 bytes>            ; 35-bit encoded byte offset
  <count: 10 bytes>               ; 35-bit encoded chunk byte count
  <data as hex nibbles>           ; 2 nibbles per source byte
  <checksum: 1 byte>
F7
```

### Path

- Destination for `.o` plug-ins: `/programs/plug-ins/<filename>.o`
- Other plug-in roots: `/programs/lua` (Lua), `/programs/three_pot` (3pot)
- ASCII bytes followed by `0x00` terminator

### 35-bit length / position encoding (10 bytes)

Only lower 32 bits are used in practice. First 5 bytes are always 0.

```
[0]  0
[1]  0
[2]  0
[3]  0
[4]  0
[5]  (value >> 28) & 0x0F        ; note 0x0F, not 0x7F
[6]  (value >> 21) & 0x7F
[7]  (value >> 14) & 0x7F
[8]  (value >>  7) & 0x7F
[9]  (value >>  0) & 0x7F
```

### Hex nibble data encoding

Each raw byte becomes two SysEx bytes:

```
high_nibble = (byte >> 4) & 0x0F
low_nibble  = (byte     ) & 0x0F
```

A 512-byte chunk becomes 1024 SysEx data bytes.

### Checksum

Sum every byte from index 7 (the first byte after the header, i.e. starting at the opcode `04`) through the end of the data payload. Negate and mask to 7 bits.

```
sum = 0
for b in message[7 : end_of_data]:
    sum += b
checksum = (-sum) & 0x7F
```

Append checksum, then `F7`.

### Chunking

- Raw chunk size: **512 bytes**
- `createAlways = 1` only for the first chunk (creates/truncates the file)
- `createAlways = 0` for all subsequent chunks (appends at `position`)
- Wait for ACK after each chunk before sending the next

### ACK message

```
F0 00 21 27 6D <id> 7A 00 04 F7
```

## Reboot (`7F`)

```
F0 00 21 27 6D <id> 7F F7
```

No response. Device restarts. Required after uploading a new `.o` plug-in so the firmware re-scans `/programs/plug-ins/` on boot.

## Deploy sequence (firmware v1.13+)

This is the flow implemented by the vendor `push_plugin_to_device.py` script.

1. Open MIDI output and input to the NT (port name typically contains "disting NT").
2. Optionally save current working state to a named preset:
   - Set preset name (`0x47`)
   - Save preset (`0x36 02`), then sleep ~500 ms (no ACK).
3. Get current preset path (`0x56`) so it can be reloaded at the end.
4. Send "new blank preset" (`0x35`). This frees any algorithm slots that reference the plug-in being replaced; the device will refuse to overwrite a loaded `.o` otherwise.
5. Read `build/arm/Hemispheres.o` and upload in 512-byte chunks:
   - Build upload-chunk SysEx (`createAlways = 1` only on the first iteration).
   - Send via MIDI output.
   - Block on MIDI input waiting for the chunk ACK.
6. Send rescan-plugins SysEx (`0x7A 0x08`, with checksum). NT re-reads `/programs/plug-ins/` and registers the new `.o`. No reboot.
7. Send load-preset SysEx (`0x34 0x00 <path> 0x00`) to restore the preset captured in step 3.

## Deploy sequence (legacy / pre-v1.13)

If `rescanPlugins` is unavailable, replace step 6 with a full reboot (`0x7F`) and rely on NT scanning `/programs/plug-ins/` during boot. Step 7 is unnecessary because the device reloads its last preset on boot.

## Open questions

- Does NT respond with an error SysEx on bad checksum, or just silently drop? Reference Python `push_plugin_to_device.py` would clarify; reasonable to assume retry on timeout.
- Is `sysExId` settable per device on the NT, and how do users normally discover it? Default 0 is the safe choice for single-device dev setups.
- After reboot, is there a sysex "ready" notification or do we poll? `requestVersion` retry-with-timeout is the safe pattern.

## Reference implementations

- Vendor Python deploy script, vendored at `harness/scripts/push_plugin_to_device.py` (MIT, mirrors `expertsleepersltd/distingNT@abe311cf:tools/push_plugin_to_device.py`).
- `nt_helper` Dart: `lib/domain/sysex/requests/request_file_upload_chunk.dart` (authoritative byte layout).
- Vendor SysEx reference: `github.com/thorinside/nt_helper/blob/main/docs/SYSEX_REFERENCE.md`.

## Running it from the repo

```bash
make deploy-sysex
# Optional: pick a different device or .o
make deploy-sysex SYSEX_ID=0 SYSEX_PLUGIN=build/arm/Hemispheres.o
```

Defaults: `SYSEX_ID=0`, `SYSEX_PLUGIN=build/arm/Hemispheres.o`. Requires NT firmware v1.13+ and only one disting NT on the MIDI bus.
