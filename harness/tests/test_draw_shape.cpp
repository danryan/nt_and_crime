#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>

static uint8_t pixel(int x, int y) {
    int byte_index = y * 128 + (x >> 1);
    return (x & 1) ? (NT_screen[byte_index] >> 4) : (NT_screen[byte_index] & 0x0f);
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
