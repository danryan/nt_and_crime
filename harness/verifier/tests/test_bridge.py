import json
import subprocess
from pathlib import Path

import pytest

from harness.verifier import parser

_BIN = Path(__file__).resolve().parents[3] / "build" / "host" / "render_dump"


def _screen(mode: str) -> list[int]:
    if not _BIN.exists():
        pytest.skip(f"{_BIN} not built; run: make build/host/render_dump")
    out = subprocess.run([str(_BIN), mode], capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


def test_bridge_numeric_render_parses_back() -> None:
    screen = _screen("numeric")
    layout = parser.Layout(first_bus=13, count=2, value_x=12, row_h=10, row_y0=12)
    assert parser.parse_numeric(screen, layout) == {13: 1.0, 14: -0.25}


def test_bridge_scope_render_parses_back() -> None:
    screen = _screen("scope")
    region = parser.ScopeRegion(x0=0, y0=0, width=256, height=64)
    r = parser.parse_scope(screen, region, sample_rate=48000, timebase=1)
    assert r.shape == "square"
    assert abs(r.frequency_hz - 1500.0) / 1500.0 < 0.15
