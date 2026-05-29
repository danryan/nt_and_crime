"""Verifier's own font glyph templates, loaded from font.json.

font.json is emitted from the verifier_logic.h glyph table, which the device
draws verbatim on both sim and hardware, so these templates are hardware-valid.
"""
from __future__ import annotations

import json
from pathlib import Path

_FONT_PATH = Path(__file__).parent / "fixtures" / "font.json"

GLYPHS: dict[str, list[int]] = {
    k: list(v) for k, v in json.loads(_FONT_PATH.read_text()).items()
}
