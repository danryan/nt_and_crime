#pragma once
#include <cstdint>
#include <distingnt/api.h>

// Vendor weegfx compatibility namespace. The ported O_C apps and the
// hand-ported menu widgets reference these types and constants. The shim does
// NOT adopt vendor weegfx::Graphics; it provides the same coordinate type,
// fixed-font geometry, and op enums so vendor sources compile against
// shim::Graphics through the existing `graphics` global. Mirrors
// vendor/O_C-Phazerville/software/src/src/drivers/weegfx.h:30-48.
namespace weegfx {

using coord_t = int_fast16_t;

static constexpr coord_t kFixedFontW = 6;
static constexpr coord_t kFixedFontH = 8;

enum PIXEL_OP {
    PIXEL_OP_OR,    // DST | SRC
    PIXEL_OP_XOR,   // DST ^ SRC
    PIXEL_OP_NAND,  // DST &= ~SRC
    PIXEL_OP_SRC,   // DST = SRC
};

enum CLEAR_FRAME { CLEAR_FRAME_DISABLE, CLEAR_FRAME_ENABLE };

}  // namespace weegfx

namespace shim {

class Graphics {
public:
    void setPrintPos(int x, int y) { print_x = x; print_y = y; }
    int  getPrintPosX() const { return print_x; }
    int  getPrintPosY() const { return print_y; }

    void print(const char* s);
    void print(int n);

    // Vendor weegfx::Graphics::printf. Relabi::View calls it for status
    // text. Variadic printf-style format; shim implementation delegates to
    // vsnprintf into a small stack buffer then forwards to print(const char*).
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    void setPixel(int x, int y);
    void drawLine(int x0, int y0, int x1, int y1, uint8_t pattern = 0xFF);
    void drawFrame(int x, int y, int w, int h);
    void drawRect(int x, int y, int w, int h);
    void invertRect(int x, int y, int w, int h);
    void clearRect(int x, int y, int w, int h);
    void drawCircle(int x, int y, int r);
    void drawBitmap8(int x, int y, int w, const uint8_t* data);

    // weegfx full-screen extensions. The vendor reference is weegfx.cpp; the
    // shim re-expresses each on the 256-wide 4-bit NT_screen via the existing
    // pixel/text model. These are additive and do NOT alter any existing
    // method. They are inline so the host test binaries that already link the
    // NT runtime (which owns NT_screen / NT_drawText) can exercise them
    // without linking shim/src/graphics.cpp.

    // Move the current text print position by a delta (vendor weegfx.h:146).
    void movePrintPos(int dx, int dy) { print_x += dx; print_y += dy; }

    // Solid horizontal run of w pixels starting at (x, y) (vendor weegfx.cpp:206).
    void drawHLine(int x, int y, int w) {
        for (int i = 0; i < w; ++i) put_pixel(x + i, y, 15);
    }

    // Horizontal run lighting every `skip`-th pixel. Vendor advances the write
    // pointer by `skip` bytes per step (vendor weegfx.cpp:284), so the lit
    // pixels are at x, x+skip, x+2*skip, ... within [x, x+w).
    void drawHLinePattern(int x, int y, int w, uint8_t skip) {
        if (skip == 0) return;
        for (int i = 0; i < w; i += skip) put_pixel(x + i, y, 15);
    }

    // Solid vertical run of h pixels starting at (x, y) (vendor weegfx.cpp:216).
    void drawVLine(int x, int y, int h) {
        for (int i = 0; i < h; ++i) put_pixel(x, y + i, 15);
    }

    // Vertical run masked by an 8-pixel `pattern` repeated down the column.
    // Bit i of `pattern` controls row (y + i) within each 8-row group, matching
    // the vendor byte-per-8-rows framebuffer (vendor weegfx.cpp:251).
    void drawVLinePattern(int x, int y, int h, uint8_t pattern) {
        for (int i = 0; i < h; ++i) {
            if (pattern & (1u << ((y + i) & 0x7))) put_pixel(x, y + i, 15);
        }
    }

    // Right-align a string ending at the current print pos; pos unchanged
    // (vendor weegfx.cpp:590). write_right is the PIXEL_OP_SRC variant; on the
    // shim's NT_drawText-backed text model both render identically
    // (vendor weegfx.cpp:603).
    void print_right(const char* s) {
        if (!s) return;
        NT_drawText(print_x, print_y + kTextBaseline, s, 15, kNT_textRight, kNT_textNormal);
    }
    void write_right(const char* s) { print_right(s); }

    // Signed value with an explicit sign for non-negative values (a leading
    // '+' or '-'), at the current print pos; advances pos like print
    // (vendor weegfx.cpp:507). Rendered inline (matching the existing print()
    // origin and 6px advance) so the weegfx extensions need no link to the
    // non-inline print() in graphics.cpp.
    void pretty_print(int value) {
        const char* s = pretty_to_buf(value);
        NT_drawText(print_x, print_y + kTextBaseline, s, 15, kNT_textLeft, kNT_textNormal);
        int len = 0;
        while (s[len]) ++len;
        print_x += len * kCharAdvance;
    }

    // pretty_print right-aligned ending at the current print pos; pos
    // unchanged (vendor weegfx.cpp:548). Zero renders as bare "0" without a sign.
    void pretty_print_right(int value) {
        if (value == 0) {
            print_right("0");
        } else {
            print_right(pretty_to_buf(value));
        }
    }

    // String at absolute coords without moving the print pos
    // (vendor weegfx.cpp:624).
    void drawStr(int x, int y, const char* s) {
        if (!s) return;
        NT_drawText(x, y + kTextBaseline, s, 15, kNT_textLeft, kNT_textNormal);
    }

    // 8-pixel-tall column bitmap drawn with source semantics (clears the
    // background bits it covers) rather than OR (vendor weegfx.cpp:314). On the
    // shim's per-pixel model this writes each of the 8 rows from the column
    // byte's bits (set bit -> white, clear bit -> black).
    void writeBitmap8(int x, int y, int w, const uint8_t* data) {
        for (int col = 0; col < w; ++col) {
            uint8_t bits = data[col];
            for (int row = 0; row < 8; ++row)
                put_pixel(x + col, y + row, (bits & (1u << row)) ? 15 : 0);
        }
    }

    // Write a raw vendor framebuffer byte: 8 vertically-stacked pixels at
    // (x, y..y+7), bit i lighting row y+i (vendor weegfx.h:135).
    void drawAlignedByte(int x, int y, uint8_t byte) {
        for (int row = 0; row < 8; ++row)
            if (byte & (1u << row)) put_pixel(x, y + row, 15);
    }

private:
    int print_x = 0;
    int print_y = 0;

    // Vendor weegfx text origin is the glyph top-left; the shim renders text
    // via NT_drawText whose y is the baseline. The existing print() applies a
    // +7 baseline offset and a 6px-per-glyph advance (shim/src/graphics.cpp);
    // the new text helpers match both.
    static constexpr int kTextBaseline = 7;
    static constexpr int kCharAdvance = 6;

    // Direct 4-bit NT_screen pixel write, identical in effect to the file-scope
    // set_pixel in graphics.cpp. Defined inline here so the weegfx extension
    // methods are self-contained: the host runtime owns NT_screen, so test
    // binaries that link only the NT runtime can exercise them.
    static void put_pixel(int x, int y, int colour) {
        if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
        int byte_index  = y * 128 + (x >> 1);
        uint8_t mask    = (x & 1) ? 0x0f : 0xf0;
        uint8_t shifted = (uint8_t)((colour & 0x0f) << ((x & 1) ? 0 : 4));
        NT_screen[byte_index] = (uint8_t)((NT_screen[byte_index] & ~mask) | shifted);
    }

    // Format `value` with an explicit leading sign into a static scratch buffer,
    // mirroring vendor itos<int,true> (vendor weegfx.cpp:469): zero renders
    // with a leading space " 0"; non-negative non-zero get a leading '+';
    // negative get a leading '-'. Returns the buffer.
    static const char* pretty_to_buf(int value) {
        static char buf[16];
        int idx = 0;
        unsigned mag = (unsigned)(value < 0 ? -(long)value : value);
        if (mag == 0) {
            buf[idx++] = ' ';  // zero: leading space
        } else {
            buf[idx++] = (value < 0) ? '-' : '+';  // nonzero: sign
        }
        char digits[12];
        int n = 0;
        if (mag == 0) digits[n++] = '0';
        while (mag) { digits[n++] = (char)('0' + mag % 10); mag /= 10; }
        while (n--) buf[idx++] = digits[n];
        buf[idx] = 0;
        return buf;
    }
};

}  // namespace shim

extern shim::Graphics graphics;
