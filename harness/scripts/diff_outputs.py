#!/usr/bin/env python3
"""Bit-faithful diff for binary outputs; tolerant diff for audio with --lsb-tolerance N."""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

EXIT_MATCH = 0
EXIT_DIVERGE = 1
EXIT_IO_ERROR = 2

SCREEN_BYTES = 8192
FLOAT32_SIZE = 4
LSB_UNIT = 2**-24  # one LSB at 24-bit converter resolution


def _read_file(path: Path) -> bytes | None:
    try:
        return path.read_bytes()
    except OSError as exc:
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        return None


def diff_bus(expected_path: Path, actual_path: Path, lsb_tolerance: int) -> int:
    """Compare float32 little-endian buffers element-by-element."""
    expected_bytes = _read_file(expected_path)
    actual_bytes = _read_file(actual_path)
    if expected_bytes is None or actual_bytes is None:
        return EXIT_IO_ERROR

    if len(expected_bytes) % FLOAT32_SIZE != 0 or len(actual_bytes) % FLOAT32_SIZE != 0:
        print(
            f"error: file sizes not multiples of 4 "
            f"(expected={len(expected_bytes)}, actual={len(actual_bytes)})",
            file=sys.stderr,
        )
        return EXIT_IO_ERROR

    n_expected = len(expected_bytes) // FLOAT32_SIZE
    n_actual = len(actual_bytes) // FLOAT32_SIZE

    if n_expected != n_actual:
        print(
            f"divergence: element count mismatch "
            f"(expected {n_expected}, actual {n_actual})"
        )
        return EXIT_DIVERGE

    tolerance = lsb_tolerance * LSB_UNIT

    for i in range(n_expected):
        offset = i * FLOAT32_SIZE
        (exp_val,) = struct.unpack_from("<f", expected_bytes, offset)
        (act_val,) = struct.unpack_from("<f", actual_bytes, offset)
        if abs(exp_val - act_val) > tolerance:
            print(
                f"divergence at index {i} (byte offset {offset}): "
                f"expected={exp_val!r}, actual={act_val!r}"
            )
            return EXIT_DIVERGE

    return EXIT_MATCH


def _write_pgm_diff(expected_bytes: bytes, actual_bytes: bytes, actual_path: Path) -> None:
    """Write a P5 PGM with differing bytes shown as white on grey background."""
    width = 64
    height = SCREEN_BYTES // width

    pgm_path = actual_path.with_suffix(actual_path.suffix + ".diff.pgm")
    pixels = bytearray(SCREEN_BYTES)

    for i in range(SCREEN_BYTES):
        if expected_bytes[i] != actual_bytes[i]:
            pixels[i] = 255  # white marks a diverging byte
        else:
            pixels[i] = 128  # grey for matching bytes

    header = f"P5\n{width} {height}\n255\n".encode()
    pgm_path.write_bytes(header + bytes(pixels))
    print(f"diff image written to {pgm_path}")


def diff_screen(expected_path: Path, actual_path: Path) -> int:
    """Compare 8192-byte screen buffers byte-by-byte."""
    expected_bytes = _read_file(expected_path)
    actual_bytes = _read_file(actual_path)
    if expected_bytes is None or actual_bytes is None:
        return EXIT_IO_ERROR

    for i in range(min(len(expected_bytes), len(actual_bytes))):
        exp_byte = expected_bytes[i]
        act_byte = actual_bytes[i]
        if exp_byte != act_byte:
            print(
                f"divergence at byte {i}: "
                f"expected=0x{exp_byte:02x} (nibbles {exp_byte >> 4:x}/{exp_byte & 0xf:x}), "
                f"actual=0x{act_byte:02x} (nibbles {act_byte >> 4:x}/{act_byte & 0xf:x})"
            )
            _write_pgm_diff(expected_bytes, actual_bytes, actual_path)
            return EXIT_DIVERGE

    if len(expected_bytes) != len(actual_bytes):
        print(
            f"divergence: size mismatch "
            f"(expected {len(expected_bytes)}, actual {len(actual_bytes)})"
        )
        return EXIT_DIVERGE

    return EXIT_MATCH


def diff_params(expected_path: Path, actual_path: Path) -> int:
    """Diff param logs as ordered text; report first divergent line."""
    expected_bytes = _read_file(expected_path)
    actual_bytes = _read_file(actual_path)
    if expected_bytes is None or actual_bytes is None:
        return EXIT_IO_ERROR

    expected_lines = expected_bytes.decode(errors="replace").splitlines()
    actual_lines = actual_bytes.decode(errors="replace").splitlines()

    for i, (exp_line, act_line) in enumerate(zip(expected_lines, actual_lines), start=1):
        if exp_line != act_line:
            print(
                f"divergence at line {i}: "
                f"expected={exp_line!r}, actual={act_line!r}"
            )
            return EXIT_DIVERGE

    if len(expected_lines) != len(actual_lines):
        print(
            f"divergence: line count mismatch "
            f"(expected {len(expected_lines)}, actual {len(actual_lines)})"
        )
        return EXIT_DIVERGE

    return EXIT_MATCH


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Bit-faithful diff for NT simulator outputs.",
        usage="diff_outputs.py KIND EXPECTED ACTUAL [--lsb-tolerance N]",
    )
    parser.add_argument("kind", choices=["bus", "screen", "params"])
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument(
        "--lsb-tolerance",
        metavar="N",
        type=int,
        default=0,
        help="For bus: allow differences up to N * 2^-24 per sample.",
    )
    args = parser.parse_args()

    match args.kind:
        case "bus":
            return diff_bus(args.expected, args.actual, args.lsb_tolerance)
        case "screen":
            return diff_screen(args.expected, args.actual)
        case "params":
            return diff_params(args.expected, args.actual)
        case _:
            print(f"error: unknown kind {args.kind!r}", file=sys.stderr)
            return EXIT_IO_ERROR


if __name__ == "__main__":
    sys.exit(main())
