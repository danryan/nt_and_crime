"""Parse the Verifier readout from a 256x64 grayscale screen array."""
from __future__ import annotations

from dataclasses import dataclass

from harness.verifier import font

GLYPH_W = 6
GLYPH_H = 8
VALUE_CHARS = 7  # sNN.fff


@dataclass(frozen=True)
class Layout:
    first_bus: int
    count: int
    value_x: int
    row_h: int
    row_y0: int


def _cell(screen: list[int], x0: int, y0: int) -> tuple[int, ...]:
    return tuple(
        1 if screen[(y0 + y) * 256 + (x0 + x)] else 0
        for y in range(GLYPH_H)
        for x in range(GLYPH_W)
    )


def _match_glyph(cell: tuple[int, ...]) -> str:
    best, best_score = "#", -1
    for ch, bmp in font.GLYPHS.items():
        score = sum(1 for a, b in zip(cell, bmp) if a == b)
        if score > best_score:
            best, best_score = ch, score
    return best


def _read_value(screen: list[int], x0: int, y0: int) -> float:
    chars = [
        _match_glyph(_cell(screen, x0 + i * GLYPH_W, y0))
        for i in range(VALUE_CHARS)
    ]
    text = "".join(chars)
    sign = -1.0 if text[0] == "-" else 1.0
    integer = int(text[1:3])
    frac = int(text[4:7])
    return sign * (integer + frac / 1000.0)


def parse_numeric(screen: list[int], layout: Layout) -> dict[int, float]:
    out: dict[int, float] = {}
    for row in range(layout.count):
        y0 = layout.row_y0 + row * layout.row_h
        out[layout.first_bus + row] = _read_value(screen, layout.value_x, y0)
    return out


@dataclass(frozen=True)
class ScopeRegion:
    x0: int
    y0: int
    width: int
    height: int


@dataclass(frozen=True)
class ScopeResult:
    samples: tuple[float, ...]
    frequency_hz: float
    shape: str


def _column_centroid(screen: list[int], region: ScopeRegion, x: int) -> float | None:
    ys = [y for y in range(region.height)
          if screen[(region.y0 + y) * 256 + (region.x0 + x)]]
    if not ys:
        return None
    mid = sum(ys) / len(ys)
    # map pixel row to a normalized [-1, 1], row 0 top (+1), bottom (-1)
    return 1.0 - 2.0 * (mid / (region.height - 1))


def _classify(samples: tuple[float, ...]) -> str:
    hi = max(samples)
    lo = min(samples)
    span = hi - lo
    if span == 0:
        return "flat"
    near_rail = sum(1 for s in samples if s > hi - 0.1 * span or s < lo + 0.1 * span)
    if near_rail / len(samples) > 0.7:
        return "square"
    return "wave"


def parse_scope(screen: list[int], region: ScopeRegion,
                sample_rate: float, timebase: int) -> ScopeResult:
    cols = [_column_centroid(screen, region, x) for x in range(region.width)]
    samples = tuple(c for c in cols if c is not None)
    crossings = [
        x for x in range(1, len(cols))
        if cols[x - 1] is not None and cols[x] is not None
        and cols[x - 1] < 0 <= cols[x]
    ]
    if len(crossings) >= 2:
        period_px = (crossings[-1] - crossings[0]) / (len(crossings) - 1)
        frequency_hz = sample_rate / (period_px * timebase)
    else:
        frequency_hz = 0.0
    return ScopeResult(samples=samples, frequency_hz=frequency_hz,
                       shape=_classify(samples))
