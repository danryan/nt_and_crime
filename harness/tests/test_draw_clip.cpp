// Q1 host-set clip rect tests. Vendor applets occasionally draw past their
// nominal 0..63 vendor-x range; without a clip rect those emits bleed into
// the neighboring lane's leftmost column on a 4-lane host (Quadrants).
//
// Clip rect lives in screen space: [HS::gfx_offset, HS::gfx_offset +
// HS::gfx_clip_w) x [HS::gfx_offset_y, HS::gfx_offset_y + HS::gfx_clip_h).
// gfx* helpers in HemisphereApplet clamp emits to this rect. Defaults
// (clip_w=256, clip_h=64) are full screen so existing tests and
// standalone runs see no behavior change.

#include "catch.hpp"
#include "nt_runtime.h"
#include "HemisphereApplet.h"
#include <distingnt/api.h>

namespace {

// Minimal HemisphereApplet subclass for driving gfx wrappers directly. All
// pure virtuals stubbed; we only call gfxPixel / gfxRect in these tests.
class ClipProbe : public HemisphereApplet {
public:
    const char* applet_name() override { return "clip"; }
    void Start() override {}
    void Controller() override {}
    void View() override {}
    uint64_t OnDataRequest() override { return 0; }
    void OnDataReceive(uint64_t) override {}
    void OnEncoderMove(int) override {}
    void SetHelp() override {}

    // Public adapters so test bodies can hit the protected gfx* helpers
    // without inheriting from HemisphereApplet at the test-case scope.
    using HemisphereApplet::gfxPixel;
    using HemisphereApplet::gfxRect;
    using HemisphereApplet::gfxFrame;
    using HemisphereApplet::gfxLine;
};

// Mirrors shim/src/graphics.cpp set_pixel nibble convention: even x -> high
// nibble, odd x -> low nibble. (nt_runtime.cpp's set_pixel uses the opposite
// convention; both conventions co-exist in the harness, so each test reads
// the nibble matching its writer. gfxPixel routes through graphics.cpp.)
uint8_t pixel(int x, int y) {
    int byte_index = y * 128 + (x >> 1);
    return (x & 1) ? (NT_screen[byte_index] & 0x0f) : (NT_screen[byte_index] >> 4);
}

void clear_screen() { for (int i = 0; i < 128 * 64; ++i) NT_screen[i] = 0; }

void reset_clip_defaults() {
    HS::gfx_offset = 0;
    HS::gfx_offset_y = 0;
    HS::gfx_clip_w = 256;
    HS::gfx_clip_h = 64;
}

}  // anonymous namespace

TEST_CASE("Q1 default clip rect: full-screen emit lands", "[draw-clip]") {
    clear_screen();
    reset_clip_defaults();

    ClipProbe probe;
    probe.gfxPixel(200, 30);

    REQUIRE(pixel(200, 30) == 15);
}

TEST_CASE("Q1 host-set lane clip: emit at vendor (63,30) lands at screen (127,30)", "[draw-clip]") {
    clear_screen();
    reset_clip_defaults();
    HS::gfx_offset = 64;
    HS::gfx_clip_w = 64;
    HS::gfx_clip_h = 64;

    ClipProbe probe;
    probe.gfxPixel(63, 30);

    REQUIRE(pixel(127, 30) == 15);

    reset_clip_defaults();
}

TEST_CASE("Q1 host-set lane clip: emit at vendor (64,30) is clipped", "[draw-clip]") {
    clear_screen();
    reset_clip_defaults();
    HS::gfx_offset = 64;
    HS::gfx_clip_w = 64;
    HS::gfx_clip_h = 64;

    ClipProbe probe;
    probe.gfxPixel(64, 30);

    REQUIRE(pixel(128, 30) == 0);

    reset_clip_defaults();
}

TEST_CASE("Q1 lower-edge clip: vendor (10,63) lands; vendor (10,64) clipped above frame", "[draw-clip]") {
    clear_screen();
    reset_clip_defaults();
    // Constrain clip to a 40-row band so 64 maps inside the framebuffer but
    // outside the clip rect (clip_h=40 -> screen y range [0, 40)).
    HS::gfx_clip_w = 256;
    HS::gfx_clip_h = 40;

    ClipProbe probe;
    probe.gfxPixel(10, 39);
    probe.gfxPixel(10, 40);

    REQUIRE(pixel(10, 39) == 15);
    REQUIRE(pixel(10, 40) == 0);

    reset_clip_defaults();
}

TEST_CASE("Q1 upper-edge clip: vendor (10,-1) is clipped", "[draw-clip]") {
    clear_screen();
    reset_clip_defaults();
    HS::gfx_clip_w = 256;
    HS::gfx_clip_h = 64;

    ClipProbe probe;
    probe.gfxPixel(10, -1);

    // Nothing in row 0 col 10 because the emit was above the clip rect.
    REQUIRE(pixel(10, 0) == 0);

    reset_clip_defaults();
}
