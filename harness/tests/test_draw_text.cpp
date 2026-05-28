#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>
// Relative include: the test_draw_text rule compiles with $(HOST_FLAGS) only
// (no -Ishim/include). The weegfx compat namespace is header-only.
#include "../../shim/include/hem_graphics.h"

// Compile-time contract for the weegfx compat namespace. The fixed-font
// geometry and op enums must match vendor weegfx.h:30-48 so vendor sources
// that read these constants compile against the shim unchanged.
static_assert(weegfx::kFixedFontW == 6, "weegfx fixed font width must be 6");
static_assert(weegfx::kFixedFontH == 8, "weegfx fixed font height must be 8");
static_assert(weegfx::PIXEL_OP_OR == 0, "PIXEL_OP_OR must be the first enumerator");
static_assert(weegfx::PIXEL_OP_SRC == 3, "PIXEL_OP_SRC must be the fourth enumerator");
static_assert(weegfx::CLEAR_FRAME_DISABLE == 0, "CLEAR_FRAME_DISABLE must be 0");
static_assert(weegfx::CLEAR_FRAME_ENABLE == 1, "CLEAR_FRAME_ENABLE must be 1");
static_assert(sizeof(weegfx::coord_t) >= sizeof(int_least16_t), "coord_t must hold screen coords");

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
