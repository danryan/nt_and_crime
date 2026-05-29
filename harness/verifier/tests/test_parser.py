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


def _stamp_square(screen: list[int], period_px: int) -> None:
    for x in range(256):
        v = 1.0 if (x % period_px) < (period_px // 2) else -1.0
        y = 16 if v > 0 else 48
        screen[y * 256 + x] = 15


def test_parse_scope_estimates_frequency_and_shape() -> None:
    screen = _blank()
    _stamp_square(screen, period_px=32)
    region = parser.ScopeRegion(x0=0, y0=0, width=256, height=64)
    result = parser.parse_scope(screen, region, sample_rate=48000, timebase=1)
    # period 32 px, timebase 1 -> 48000 / 32 = 1500 Hz, within 10 percent.
    assert abs(result.frequency_hz - 1500.0) / 1500.0 < 0.1
    assert result.shape == "square"


def test_parse_scope_blank_screen_is_flat_not_crash() -> None:
    # A silent or disconnected scope bus leaves no lit pixels. The parser must
    # report flat with zero frequency, not raise on max() of an empty series.
    screen = _blank()
    region = parser.ScopeRegion(x0=0, y0=0, width=256, height=64)
    result = parser.parse_scope(screen, region, sample_rate=48000, timebase=1)
    assert result.samples == ()
    assert result.shape == "flat"
    assert result.frequency_hz == 0.0
