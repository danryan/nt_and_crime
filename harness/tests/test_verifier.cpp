#include "catch.hpp"
#include <string>
#include <cstring>
#include <cstdint>
#include "../../plugins/probes/verifier_logic.h"

using namespace verifier;

// Declared by Verifier.cpp for host tests.
enum { kP_View, kP_First, kP_Count, kP_Mode, kP_Reset, kP_ScopeBus, kP_Timebase };
enum { kReset_Off = 0, kReset_On = 1 };
extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data);
float verifier_mean_for_test(_NT_algorithm* self, int row);

TEST_CASE("reduction accumulates mean min max pkpk", "[verifier][reduce]") {
    Reduction r;
    reduction_reset(r);
    const float a[] = {1.0f, -1.0f, 0.5f};
    reduction_accumulate(r, a, 3);
    REQUIRE(reduction_value(r, kMin)  == Catch::Approx(-1.0f));
    REQUIRE(reduction_value(r, kMax)  == Catch::Approx(1.0f));
    REQUIRE(reduction_value(r, kPkPk) == Catch::Approx(2.0f));
    REQUIRE(reduction_value(r, kMean) == Catch::Approx(0.5f / 3.0f * 1.0f).margin(0.0001));
}

TEST_CASE("reduction spans multiple accumulate calls", "[verifier][reduce]") {
    Reduction r;
    reduction_reset(r);
    const float a[] = {2.0f, 2.0f};
    const float b[] = {4.0f, 4.0f};
    reduction_accumulate(r, a, 2);
    reduction_accumulate(r, b, 2);
    REQUIRE(reduction_value(r, kMean) == Catch::Approx(3.0f));
    REQUIRE(reduction_value(r, kMax)  == Catch::Approx(4.0f));
}

TEST_CASE("empty reduction reads zero", "[verifier][reduce]") {
    Reduction r;
    reduction_reset(r);
    REQUIRE(reduction_value(r, kMean) == Catch::Approx(0.0f));
}

TEST_CASE("volts_to_millivolts rounds away from zero", "[verifier][fmt]") {
    REQUIRE(volts_to_millivolts(1.2345f) == 1235);   // .2345 -> rounds 1234.5 up
    REQUIRE(volts_to_millivolts(-1.2345f) == -1235);
    REQUIRE(volts_to_millivolts(0.0f) == 0);
}

TEST_CASE("format_mv fixed width sNN.fff with leading zeros", "[verifier][fmt]") {
    char b[8];
    format_mv(1000, b);  REQUIRE(std::string(b) == "+01.000");
    format_mv(-250, b);  REQUIRE(std::string(b) == "-00.250");
    format_mv(0, b);     REQUIRE(std::string(b) == "+00.000");
    format_mv(99999, b); REQUIRE(std::string(b) == "+99.999");
}

TEST_CASE("format_mv flags overflow with sentinel", "[verifier][fmt]") {
    char b[8];
    format_mv(100000, b);
    REQUIRE(b[0] == '+');
    REQUIRE(std::string(b).find('#') != std::string::npos);
}

TEST_CASE("scope_push decimates and freezes when full", "[verifier][scope]") {
    float buf[kScopeWidth];
    int wr = 0, phase = 0; bool filled = false;
    // 512 samples at decim=2 -> exactly 256 kept, then frozen.
    float chunk[64];
    for (int i = 0; i < 64; ++i) chunk[i] = (float)i;
    int pushed = 0;
    while (!filled && pushed < 16) { scope_push(buf, wr, phase, filled, chunk, 64, 2); ++pushed; }
    REQUIRE(filled);
    REQUIRE(wr == kScopeWidth);
    // first kept sample is chunk[0]=0, decim=2 keeps even indices
    REQUIRE(buf[0] == Catch::Approx(0.0f));
    REQUIRE(buf[1] == Catch::Approx(2.0f));
}

TEST_CASE("scope_trigger finds first rising zero-cross", "[verifier][scope]") {
    float buf[8] = {-1, -0.5f, 0.5f, 1, 0.5f, -0.5f, -1, 0.2f};
    REQUIRE(scope_trigger(buf, 8) == 2);   // buf[1]<0, buf[2]>=0
}

TEST_CASE("scope_trigger falls back to 0 when no crossing", "[verifier][scope]") {
    float buf[4] = {1, 1, 1, 1};   // DC, no rising zero-cross
    REQUIRE(scope_trigger(buf, 4) == 0);
}

#include "nt_runtime.h"

// Count lit pixels in a [x0,x1) x [y0,y1) screen window, using the sim's nibble
// convention (odd x -> high nibble), matching nt_runtime's set_pixel.
static int lit_in_window(int x0, int y0, int x1, int y1) {
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int byte = y * 128 + (x >> 1);
            uint8_t b = NT_screen[byte];
            uint8_t nib = (x & 1) ? (b >> 4) : (b & 0x0f);
            if (nib) ++count;
        }
    }
    return count;
}

TEST_CASE("render_numeric lights a glyph in each value cell per row", "[verifier][render]") {
    nt::reset_runtime();
    const int   buses[2]  = {13, 14};
    const float values[2] = {1.000f, -0.250f};
    render_numeric(buses, values, 2);
    int row0 = lit_in_window(kValueX, kTitleBarH,
                             kValueX + kValueChars * kGlyphW, kTitleBarH + kRowH);
    int row1 = lit_in_window(kValueX, kTitleBarH + kRowH,
                             kValueX + kValueChars * kGlyphW, kTitleBarH + 2 * kRowH);
    REQUIRE(row0 > 0);
    REQUIRE(row1 > 0);
}

TEST_CASE("render_numeric is deterministic and digit-dependent per cell", "[verifier][render]") {
    const int buses[1] = {1};
    const int cell_x   = kValueX + 6 * kGlyphW;   // 7th glyph (last fraction digit)

    nt::reset_runtime();
    const float v0[1] = {0.000f};
    render_numeric(buses, v0, 1);
    int zero_cell  = lit_in_window(cell_x, kTitleBarH, cell_x + kGlyphW, kTitleBarH + kRowH);

    nt::reset_runtime();
    render_numeric(buses, v0, 1);
    int zero_again = lit_in_window(cell_x, kTitleBarH, cell_x + kGlyphW, kTitleBarH + kRowH);
    REQUIRE(zero_cell == zero_again);     // deterministic

    nt::reset_runtime();
    const float v1[1] = {0.001f};
    render_numeric(buses, v1, 1);
    int one_cell = lit_in_window(cell_x, kTitleBarH, cell_x + kGlyphW, kTitleBarH + kRowH);
    REQUIRE(one_cell != zero_cell);       // '1' glyph differs from '0' glyph
    REQUIRE(one_cell > 0);
}

TEST_CASE("render_scope draws a trace within the screen", "[verifier][render]") {
    nt::reset_runtime();
    float buf[kScopeWidth];
    for (int i = 0; i < kScopeWidth; ++i)
        buf[i] = (i % 32 < 16) ? 1.0f : -1.0f;
    render_scope(buf, kScopeWidth, scope_trigger(buf, kScopeWidth), 5.0f);
    REQUIRE(lit_in_window(0, 0, 256, 64) > 0);
}

TEST_CASE("render_numeric clears the firmware title bar band", "[verifier][render]") {
    nt::reset_runtime();
    const int   buses[1]  = {13};
    const float values[1] = {1.000f};
    render_numeric(buses, values, 1);
    // Nothing may land in the top band the firmware overpaints with its title.
    REQUIRE(lit_in_window(0, 0, 256, kTitleBarH) == 0);
    // Row 0 content lands just below the band, where it is visible on device.
    REQUIRE(lit_in_window(kValueX, kTitleBarH,
                          kValueX + kValueChars * kGlyphW, kTitleBarH + kRowH) > 0);
}

static const _NT_factory* verifier_factory() {
    return (const _NT_factory*)pluginEntry(kNT_selector_factoryInfo, 0);
}

TEST_CASE("verifier registers a factory with the Vrfy guid", "[verifier][wrap]") {
    const _NT_factory* f = verifier_factory();
    REQUIRE(f != nullptr);
    REQUIRE(f->guid == NT_MULTICHAR('V','r','f','y'));
}

TEST_CASE("verifier step accumulates the read bus; reset clears", "[verifier][wrap]") {
    nt::reset_runtime();
    const _NT_factory* f = verifier_factory();

    _NT_algorithmRequirements req{};
    int32_t specs[1] = {0};
    f->calculateRequirements(req, specs);
    static uint8_t sram[8192];
    _NT_algorithmMemoryPtrs ptrs{};
    ptrs.sram = sram;
    _NT_algorithm* alg = f->construct(ptrs, req, specs);
    REQUIRE(alg != nullptr);

    int16_t v[16] = {0};
    alg->v = v;
    v[kP_View]     = 0;   // Numeric
    v[kP_First]    = 13;
    v[kP_Count]    = 1;
    v[kP_Mode]     = 0;   // Mean
    v[kP_Reset]    = 0;
    v[kP_ScopeBus] = 13;
    v[kP_Timebase] = 1;

    int nf = nt::bus_frame_count();
    float* b13 = nt::bus_pointer(13, nf);
    for (int i = 0; i < nf; ++i) b13[i] = 2.0f;

    f->step(alg, nt::bus_frames_base(), nf / 4);
    f->step(alg, nt::bus_frames_base(), nf / 4);
    REQUIRE(verifier_mean_for_test(alg, 0) == Catch::Approx(2.0f));

    v[kP_Reset] = kReset_On;
    f->parameterChanged(alg, kP_Reset);
    REQUIRE(verifier_mean_for_test(alg, 0) == Catch::Approx(0.0f));
}
