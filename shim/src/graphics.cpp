#include "hem_graphics.h"
#include <distingnt/api.h>
#include <cstring>
#include <cstdlib>

shim::Graphics graphics;

namespace {

inline void set_pixel(int x, int y, int colour) {
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
    int byte_index  = y * 128 + (x >> 1);
    uint8_t mask    = (x & 1) ? 0x0f : 0xf0;
    uint8_t shifted = (uint8_t)((colour & 0x0f) << ((x & 1) ? 0 : 4));
    NT_screen[byte_index] = (uint8_t)((NT_screen[byte_index] & ~mask) | shifted);
}

constexpr int kCharAdvance = 6;
constexpr int kCharBaselineOffset = 7;

}  // anonymous namespace

namespace shim {

void Graphics::print(const char* s) {
    if (!s) return;
    NT_drawText(print_x, print_y + kCharBaselineOffset, s, 15, kNT_textLeft, kNT_textNormal);
    int len = 0;
    while (s[len]) ++len;
    print_x += len * kCharAdvance;
}

void Graphics::print(int n) {
    char buf[12];
    int written = NT_intToString(buf, n);
    buf[written] = 0;
    print(buf);
}

void Graphics::setPixel(int x, int y) { set_pixel(x, y, 15); }

void Graphics::drawLine(int x0, int y0, int x1, int y1, uint8_t pattern) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int step = 0;
    for (;;) {
        if (pattern & (1u << (step & 7))) set_pixel(x0, y0, 15);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        ++step;
    }
}

void Graphics::drawFrame(int x, int y, int w, int h) {
    drawLine(x, y, x + w - 1, y);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1);
    drawLine(x, y + h - 1, x + w - 1, y + h - 1);
    drawLine(x, y, x, y + h - 1);
}

void Graphics::drawRect(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            set_pixel(xx, yy, 15);
}

void Graphics::invertRect(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            int byte_index = yy * 128 + (xx >> 1);
            if (xx < 0 || xx >= 256 || yy < 0 || yy >= 64) continue;
            uint8_t mask = (xx & 1) ? 0x0f : 0xf0;
            uint8_t old  = NT_screen[byte_index] & mask;
            uint8_t flip = (uint8_t)((~old) & mask);
            NT_screen[byte_index] = (uint8_t)((NT_screen[byte_index] & ~mask) | flip);
        }
    }
}

void Graphics::clearRect(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            set_pixel(xx, yy, 0);
}

void Graphics::drawCircle(int x0, int y0, int r) {
    int x = r, y = 0, err = 0;
    while (x >= y) {
        set_pixel(x0 + x, y0 + y, 15);
        set_pixel(x0 + y, y0 + x, 15);
        set_pixel(x0 - y, y0 + x, 15);
        set_pixel(x0 - x, y0 + y, 15);
        set_pixel(x0 - x, y0 - y, 15);
        set_pixel(x0 - y, y0 - x, 15);
        set_pixel(x0 + y, y0 - x, 15);
        set_pixel(x0 + x, y0 - y, 15);
        ++y;
        if (err <= 0) { err += 2 * y + 1; }
        else          { --x; err -= 2 * x + 1; }
    }
}

void Graphics::drawBitmap8(int x, int y, int w, const uint8_t* data) {
    for (int col = 0; col < w; ++col) {
        uint8_t bits = data[col];
        for (int row = 0; row < 8; ++row) {
            if (bits & (1u << row)) set_pixel(x + col, y + row, 15);
        }
    }
}

}  // namespace shim
