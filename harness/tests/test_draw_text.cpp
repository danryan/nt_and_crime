#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>

static int count_lit_pixels() {
    int count = 0;
    for (size_t i = 0; i < 128 * 64; ++i) {
        // 4-bit packed: low nibble + high nibble
        if (NT_screen[i] & 0x0f) ++count;
        if (NT_screen[i] & 0xf0) ++count;
    }
    return count;
}

TEST_CASE("NT_drawText writes non-zero pixels at the expected row", "[draw]") {
    nt::reset_runtime();
    NT_drawText(0, 10, "A", 15, kNT_textLeft, kNT_textNormal);
    REQUIRE(count_lit_pixels() > 0);
}

TEST_CASE("NT_drawText with empty string is a no-op", "[draw]") {
    nt::reset_runtime();
    NT_drawText(0, 10, "", 15);
    REQUIRE(count_lit_pixels() == 0);
}
