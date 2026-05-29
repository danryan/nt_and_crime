from harness.verifier import font, parser


def _blank() -> list[int]:
    return [0] * (256 * 64)


def _stamp(screen: list[int], glyph: str, x0: int, y0: int) -> None:
    bmp = font.GLYPHS[glyph]
    for y in range(8):
        for x in range(6):
            if bmp[y * 6 + x]:
                screen[(y0 + y) * 256 + (x0 + x)] = 15


def _stamp_value(screen: list[int], text: str, x0: int, y0: int) -> None:
    for i, ch in enumerate(text):
        _stamp(screen, ch, x0 + i * 6, y0)


def test_parse_numeric_recovers_known_voltages() -> None:
    screen = _blank()
    layout = parser.Layout(first_bus=13, count=2, value_x=12, row_h=10, row_y0=0)
    _stamp_value(screen, "+01.000", 12, 0)
    _stamp_value(screen, "-00.250", 12, 10)
    result = parser.parse_numeric(screen, layout)
    assert result == {13: 1.000, 14: -0.250}
