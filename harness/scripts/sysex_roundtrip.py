#!/usr/bin/env python3
"""Send a .syx file to the NT and capture inbound SysEx reply.

Usage:
    python3 harness/scripts/sysex_roundtrip.py <request.syx> [--timeout 2.0] [--port-substr "disting NT"]
    python3 harness/scripts/sysex_roundtrip.py --hex "F0 7D 01 F7"

Prints hex dump of each inbound SysEx message received within the timeout window.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import mido


def find_port(direction: str, substr: str) -> str:
    func = mido.get_input_names if direction == "input" else mido.get_output_names
    ports = func()
    matches = [p for p in ports if substr.lower() in p.lower()]
    if not matches:
        raise SystemExit(
            f"no {direction} port matching {substr!r}; available: {ports}"
        )
    return matches[0]


def parse_hex(text: str) -> bytes:
    return bytes(int(tok, 16) for tok in text.replace(",", " ").split())


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("syx_file", nargs="?", type=Path, help="path to .syx request file")
    p.add_argument("--hex", help="inline hex bytes, e.g. 'F0 7D 01 F7'")
    p.add_argument("--timeout", type=float, default=2.0)
    p.add_argument("--port-substr", default="disting NT")
    args = p.parse_args()

    if args.hex:
        request = parse_hex(args.hex)
    elif args.syx_file:
        request = args.syx_file.read_bytes()
    else:
        p.error("provide either a .syx file or --hex bytes")

    if request[:1] != b"\xf0" or request[-1:] != b"\xf7":
        print(f"warn: request not wrapped in F0..F7 (got {request[:1].hex()}..{request[-1:].hex()})",
              file=sys.stderr)

    in_port_name = find_port("input", args.port_substr)
    out_port_name = find_port("output", args.port_substr)
    print(f"in:  {in_port_name}", file=sys.stderr)
    print(f"out: {out_port_name}", file=sys.stderr)

    received: list[bytes] = []
    with mido.open_input(in_port_name) as in_port, mido.open_output(out_port_name) as out_port:
        for _ in in_port.iter_pending():
            pass

        msg = mido.Message.from_bytes(list(request))
        out_port.send(msg)
        print(f"sent {len(request)} bytes: {request.hex()}", file=sys.stderr)

        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            for m in in_port.iter_pending():
                if m.type == "sysex":
                    raw = bytes([0xF0] + list(m.data) + [0xF7])
                    received.append(raw)
            time.sleep(0.01)

    print(f"received {len(received)} sysex message(s)", file=sys.stderr)
    for i, raw in enumerate(received):
        print(f"--- msg {i}: {len(raw)} bytes ---")
        for offset in range(0, len(raw), 16):
            chunk = raw[offset:offset + 16]
            hex_part = " ".join(f"{b:02x}" for b in chunk)
            print(f"  {offset:04x}  {hex_part}")

    return 0 if received else 1


if __name__ == "__main__":
    sys.exit(main())
