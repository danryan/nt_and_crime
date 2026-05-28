#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>
// Relative include: the test_draw_shape rule compiles with $(HOST_FLAGS) only
// (no -Ishim/include). The weegfx extension methods are header-inline so this
// header alone is enough; NT_screen is owned by the linked NT runtime.
#include "../../shim/include/hem_graphics.h"

static uint8_t pixel(int x, int y) {
    int byte_index = y * 128 + (x >> 1);
    return (x & 1) ? (NT_screen[byte_index] >> 4) : (NT_screen[byte_index] & 0x0f);
}

// shim::Graphics packs the 4-bit framebuffer with even x in the HIGH nibble and
// odd x in the LOW nibble, the empirically-verified NT hardware convention
// (docs/shim-additions.md). This is the OPPOSITE of the harness runtime's
// NT_drawShapeI packing used by pixel() above, so weegfx-extension assertions
// must read with this reader to match what shim writes.
static uint8_t shim_pixel(int x, int y) {
    int byte_index = y * 128 + (x >> 1);
    return (x & 1) ? (NT_screen[byte_index] & 0x0f) : (NT_screen[byte_index] >> 4);
}

TEST_CASE("NT_drawShapeI line is monotonic and contiguous", "[draw]") {
    nt::reset_runtime();
    NT_drawShapeI(kNT_line, 0, 0, 10, 0, 15);
    for (int x = 0; x <= 10; ++x) {
        REQUIRE(pixel(x, 0) == 15);
    }
    REQUIRE(pixel(11, 0) == 0);
}

TEST_CASE("NT_drawShapeI rectangle fills the interior", "[draw]") {
    nt::reset_runtime();
    NT_drawShapeI(kNT_rectangle, 2, 3, 5, 6, 7);
    for (int x = 2; x <= 5; ++x)
        for (int y = 3; y <= 6; ++y)
            REQUIRE(pixel(x, y) == 7);
}

TEST_CASE("NT_drawShapeF is currently a placeholder", "[draw]") {
    REQUIRE(nt::shape_rasteriser_is_placeholder());
}

TEST_CASE("NT_drawShapeF degenerates to NT_drawShapeI behaviour for integer coords", "[draw]") {
    nt::reset_runtime();
    NT_drawShapeF(kNT_line, 0.0f, 0.0f, 10.0f, 0.0f, 15.0f);
    for (int x = 0; x <= 10; ++x) REQUIRE(pixel(x, 0) == 15);
}

// --- weegfx full-screen extensions on shim::Graphics ---

TEST_CASE("Graphics::drawHLine lights a contiguous run and nothing past it", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.drawHLine(3, 5, 4);
    for (int x = 3; x < 7; ++x) REQUIRE(shim_pixel(x, 5) == 15);
    REQUIRE(shim_pixel(2, 5) == 0);
    REQUIRE(shim_pixel(7, 5) == 0);
}

TEST_CASE("Graphics::drawVLine lights a contiguous vertical run", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.drawVLine(10, 2, 4);
    for (int y = 2; y < 6; ++y) REQUIRE(shim_pixel(10, y) == 15);
    REQUIRE(shim_pixel(10, 1) == 0);
    REQUIRE(shim_pixel(10, 6) == 0);
}

TEST_CASE("Graphics::drawHLinePattern lights every skip-th pixel", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.drawHLinePattern(0, 0, 8, 2);
    REQUIRE(shim_pixel(0, 0) == 15);
    REQUIRE(shim_pixel(1, 0) == 0);
    REQUIRE(shim_pixel(2, 0) == 15);
    REQUIRE(shim_pixel(3, 0) == 0);
    REQUIRE(shim_pixel(6, 0) == 15);
}

TEST_CASE("Graphics::drawVLinePattern masks rows by the pattern bits", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    // pattern 0b00000101 -> rows 0 and 2 of each 8-row group lit.
    g.drawVLinePattern(4, 0, 8, 0x05);
    REQUIRE(shim_pixel(4, 0) == 15);
    REQUIRE(shim_pixel(4, 1) == 0);
    REQUIRE(shim_pixel(4, 2) == 15);
    REQUIRE(shim_pixel(4, 3) == 0);
}

TEST_CASE("Graphics::drawAlignedByte stacks 8 vertical pixels from the byte bits", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    // 0b00000011 -> rows 0 and 1 lit at the column.
    g.drawAlignedByte(20, 8, 0x03);
    REQUIRE(shim_pixel(20, 8) == 15);
    REQUIRE(shim_pixel(20, 9) == 15);
    REQUIRE(shim_pixel(20, 10) == 0);
}

TEST_CASE("Graphics::writeBitmap8 writes set bits white and clears unset bits", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    // Pre-light a pixel that the column's clear bit must blank out.
    g.drawAlignedByte(30, 0, 0x02);  // light row 1 at column 30
    REQUIRE(shim_pixel(30, 1) == 15);
    const uint8_t col[1] = { 0x01 };  // only row 0 set
    g.writeBitmap8(30, 0, 1, col);
    REQUIRE(shim_pixel(30, 0) == 15);  // bit 0 set -> white
    REQUIRE(shim_pixel(30, 1) == 0);   // bit 1 clear -> blanked (SRC semantics)
}

TEST_CASE("Graphics::movePrintPos advances the print position by the delta", "[draw][weegfx]") {
    shim::Graphics g;
    g.setPrintPos(10, 20);
    g.movePrintPos(4, -3);
    REQUIRE(g.getPrintPosX() == 14);
    REQUIRE(g.getPrintPosY() == 17);
}

TEST_CASE("Graphics::drawStr renders without moving the print position", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.setPrintPos(5, 5);
    g.drawStr(40, 30, "Hi");
    REQUIRE(g.getPrintPosX() == 5);
    REQUIRE(g.getPrintPosY() == 5);
}

TEST_CASE("Graphics::pretty_print prefixes a sign and advances the print pos", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.setPrintPos(0, 0);
    g.pretty_print(7);
    // "+7" is two glyphs; print advances by 6px per glyph (shim kCharAdvance).
    REQUIRE(g.getPrintPosX() == 12);
}

TEST_CASE("Graphics::pretty_print_right leaves the print pos unchanged", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.setPrintPos(100, 0);
    g.pretty_print_right(-3);
    REQUIRE(g.getPrintPosX() == 100);
    REQUIRE(g.getPrintPosY() == 0);
}

TEST_CASE("Graphics::print_right leaves the print pos unchanged", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.setPrintPos(120, 8);
    g.print_right("ABC");
    REQUIRE(g.getPrintPosX() == 120);
    REQUIRE(g.getPrintPosY() == 8);
}

TEST_CASE("Graphics::pretty_print renders zero with a leading space", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.setPrintPos(0, 0);
    g.pretty_print(0);
    // " 0" is two glyphs; print advances by 6px per glyph (shim kCharAdvance).
    REQUIRE(g.getPrintPosX() == 12);
}

TEST_CASE("Graphics::pretty_print_right renders zero as bare '0' with no sign", "[draw][weegfx]") {
    nt::reset_runtime();
    shim::Graphics g;
    g.setPrintPos(100, 0);
    g.pretty_print_right(0);
    REQUIRE(g.getPrintPosX() == 100);
    REQUIRE(g.getPrintPosY() == 0);
}
