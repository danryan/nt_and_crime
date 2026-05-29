# Verifier probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Verifier, an on-device NT measurement plug-in that reads any bus and renders a deterministic, screenshot-parseable readout, plus the host python parser that recovers the values.

**Architecture:** Pure measurement and formatting logic in a header (`verifier_logic.h`), including a self-drawn 6x8 bitmap font rendered via `NT_drawShapeI(kNT_point)`; a thin `_NT_algorithm` wrapper (`Verifier.cpp`) that wires parameters and buses to that logic; a C++ tool that emits the glyph table to font.json as the parser's template source; and a python parser package that template-matches the readout. TDD throughout: pure logic and pixel rendering in Catch2, digit and shape recovery in pytest.

**Tech Stack:** C++17 (shim toolchain, Catch2, nt_runtime sim), python3 (pytest, the existing `mido` screenshot path), Make.

**Spec:** `docs/superpowers/specs/2026-05-28-verifier-probe-design.md`

---

## Execution model

Sequential, not parallel. This is one cohesive unit (one plug-in, one parser) with load-bearing ordering: the wrapper depends on the logic header, the parser depends on the font dump, the font dump depends on the render. No worktree fan-out. Work is already in the worktree on branch `worktree-dr+verifier-probe` (created via `superpowers:using-git-worktrees`); submodules initialized there.

## File structure

- Create `plugins/probes/verifier_logic.h`: pure reductions, millivolt formatting, scope decimation and trigger, plus the two render helpers that call the NT draw API. One responsibility: the measurement and rendering primitives, free of the `_NT_algorithm` lifecycle.
- Create `plugins/probes/Verifier.cpp`: the `_NT_algorithm` wrapper. Struct, parameter table, lifecycle callbacks, `pluginEntry`. Wires params and buses to the header.
- Create `harness/tests/test_verifier.cpp`: Catch2 tests for the pure logic and the rendered pixels.
- Create `harness/tools/dump_font.cpp`: emits Verifier's own glyph table to `harness/verifier/fixtures/font.json`.
- Create `harness/verifier/__init__.py`, `harness/verifier/font.py`, `harness/verifier/parser.py`: the python parser package.
- Create `harness/verifier/tests/test_parser.py`: pytest for the parser.
- Create `harness/verifier/fixtures/font.json`: generated, the parser's template source.
- Create `requirements-dev.txt`: adds `pytest`.
- Modify `Makefile`: ARM build rule, `arm:` target slot, `build/host/test_verifier` rule, `build/host/dump_font` rule, a `test-verifier` convenience target.

## Conventions used below

- Build a host test: `make build/host/<name>` then `./build/host/<name>`.
- Run a single Catch2 case by tag: `./build/host/test_verifier '[tag]'`.
- ARM build sanity: `make build/arm/Verifier.o`.

---

### Task 1: Pure reductions

**Files:**
- Create: `plugins/probes/verifier_logic.h`
- Create: `harness/tests/test_verifier.cpp`
- Modify: `Makefile` (add the `build/host/test_verifier` rule)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_verifier.cpp`:

```cpp
#include "catch.hpp"
#include "../../plugins/probes/verifier_logic.h"

using namespace verifier;

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
```

Add the Makefile rule near the other `build/host/test_*` rules (for example after the `build/host/test_draw_text` rule around line 60):

```makefile
build/host/test_verifier: harness/tests/test_verifier.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make build/host/test_verifier`
Expected: FAIL to compile, `verifier_logic.h` not found / `Reduction` undefined.

- [ ] **Step 3: Write minimal implementation**

Create `plugins/probes/verifier_logic.h`:

```cpp
#pragma once
#include <cstdint>
#include <distingnt/api.h>

namespace verifier {

constexpr int kMaxBuses   = 6;
constexpr int kScopeWidth = 256;
constexpr int kGlyphW     = 6;
constexpr int kRowH       = 10;
constexpr int kValueChars = 7;   // sNN.fff
constexpr int kValueX     = 12;  // value glyph origin x (after a 2-digit label)

enum NumericMode { kMean = 0, kMin = 1, kMax = 2, kPkPk = 3 };
enum ViewMode    { kNumeric = 0, kScope = 1 };

struct Reduction {
    double   sum;
    uint32_t count;
    float    vmin;
    float    vmax;
};

inline void reduction_reset(Reduction& r) {
    r.sum = 0.0; r.count = 0; r.vmin = 0.0f; r.vmax = 0.0f;
}

inline void reduction_accumulate(Reduction& r, const float* samples, int n) {
    for (int i = 0; i < n; ++i) {
        float s = samples[i];
        if (r.count == 0) { r.vmin = s; r.vmax = s; }
        else { if (s < r.vmin) r.vmin = s; if (s > r.vmax) r.vmax = s; }
        r.sum += (double)s;
        ++r.count;
    }
}

inline float reduction_value(const Reduction& r, int mode) {
    if (r.count == 0) return 0.0f;
    switch (mode) {
        case kMean: return (float)(r.sum / (double)r.count);
        case kMin:  return r.vmin;
        case kMax:  return r.vmax;
        case kPkPk: return r.vmax - r.vmin;
    }
    return 0.0f;
}

}  // namespace verifier
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make build/host/test_verifier && ./build/host/test_verifier '[reduce]'`
Expected: PASS, 3 test cases.

- [ ] **Step 5: Commit**

```bash
git add plugins/probes/verifier_logic.h harness/tests/test_verifier.cpp Makefile
git commit -m "feat(verifier): pure measurement reductions"
```

---

### Task 2: Millivolt formatting

**Files:**
- Modify: `plugins/probes/verifier_logic.h`
- Modify: `harness/tests/test_verifier.cpp`

- [ ] **Step 1: Write the failing test**

Append to `harness/tests/test_verifier.cpp`:

```cpp
#include <cstring>

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
```

Add `#include <string>` at the top of the test file if not present.

- [ ] **Step 2: Run test to verify it fails**

Run: `make build/host/test_verifier`
Expected: FAIL, `format_mv` / `volts_to_millivolts` undefined.

- [ ] **Step 3: Write minimal implementation**

Add to `plugins/probes/verifier_logic.h` inside the namespace, after `reduction_value`:

```cpp
inline int volts_to_millivolts(float v) {
    return (int)(v * 1000.0f + (v >= 0.0f ? 0.5f : -0.5f));
}

// Writes the 7-char fixed-width form sNN.fff plus a NUL into out[8].
// Overflow (|mv| > 99999) yields a sentinel with '#' glyphs.
inline void format_mv(int mv, char out[8]) {
    char sign = mv < 0 ? '-' : '+';
    long a = mv < 0 ? -(long)mv : (long)mv;
    if (a > 99999) {
        out[0] = sign;
        out[1] = '#'; out[2] = '#'; out[3] = '#';
        out[4] = '#'; out[5] = '#'; out[6] = '#';
        out[7] = 0;
        return;
    }
    int ip = (int)(a / 1000);   // 0..99
    int fr = (int)(a % 1000);   // 0..999
    out[0] = sign;
    out[1] = (char)('0' + (ip / 10));
    out[2] = (char)('0' + (ip % 10));
    out[3] = '.';
    out[4] = (char)('0' + (fr / 100));
    out[5] = (char)('0' + ((fr / 10) % 10));
    out[6] = (char)('0' + (fr % 10));
    out[7] = 0;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make build/host/test_verifier && ./build/host/test_verifier '[fmt]'`
Expected: PASS, 3 cases.

- [ ] **Step 5: Commit**

```bash
git add plugins/probes/verifier_logic.h harness/tests/test_verifier.cpp
git commit -m "feat(verifier): millivolt fixed-width formatting"
```

---

### Task 3: Scope decimation and trigger

**Files:**
- Modify: `plugins/probes/verifier_logic.h`
- Modify: `harness/tests/test_verifier.cpp`

- [ ] **Step 1: Write the failing test**

Append to `harness/tests/test_verifier.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make build/host/test_verifier`
Expected: FAIL, `scope_push` / `scope_trigger` undefined.

- [ ] **Step 3: Write minimal implementation**

Add to `plugins/probes/verifier_logic.h` inside the namespace:

```cpp
// Fills buf left-to-right, keeping every decim-th sample, until kScopeWidth
// samples are captured, then freezes (one-shot). Re-arm by resetting wr/phase/
// filled to 0/0/false.
inline void scope_push(float* buf, int& wr, int& phase, bool& filled,
                       const float* samples, int n, int decim) {
    if (filled) return;
    if (decim < 1) decim = 1;
    for (int i = 0; i < n; ++i) {
        if (phase == 0 && wr < kScopeWidth) buf[wr++] = samples[i];
        phase = (phase + 1) % decim;
        if (wr >= kScopeWidth) { filled = true; break; }
    }
}

// Returns the index of the first rising zero-crossing (prev < 0 <= cur), or 0
// when none exists (untriggered fallback for DC or silence).
inline int scope_trigger(const float* buf, int len) {
    for (int i = 1; i < len; ++i) {
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) return i;
    }
    return 0;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make build/host/test_verifier && ./build/host/test_verifier '[scope]'`
Expected: PASS, 3 cases.

- [ ] **Step 5: Commit**

```bash
git add plugins/probes/verifier_logic.h harness/tests/test_verifier.cpp
git commit -m "feat(verifier): scope decimation and zero-cross trigger"
```

---

### Task 4: Render helpers and pixel tests

**Files:**
- Modify: `plugins/probes/verifier_logic.h`
- Modify: `harness/tests/test_verifier.cpp`

Verifier draws its own fixed 6x8 bitmap font, not the firmware font through `NT_drawText`. The sim's `NT_drawText` font is a placeholder (every glyph an identical solid block, `harness/src/font_placeholder.cpp`), and the firmware font is opaque to the parser, so a self-drawn font is required for in-sim and hardware-valid digit recovery. Verifier lights each glyph pixel via `NT_drawShapeI(kNT_point, x, y, x, y)`, routing through the active backend's `set_pixel` (the sim in tests, the firmware on hardware), so it is correct on both despite the sim and shim packing opposite nibbles. `render_scope` uses `kNT_line`. The C++ test asserts cells are lit and value-dependent; full digit recovery is the python parser's job (Task 7). The `lit_in_window` reader uses the sim's nibble convention (odd x -> high nibble), matching the sim's `set_pixel`.

- [ ] **Step 1: Write the failing test**

Append to `harness/tests/test_verifier.cpp`:

```cpp
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
    int row0 = lit_in_window(kValueX, 0, kValueX + kValueChars * kGlyphW, kRowH);
    int row1 = lit_in_window(kValueX, kRowH, kValueX + kValueChars * kGlyphW, 2 * kRowH);
    REQUIRE(row0 > 0);
    REQUIRE(row1 > 0);
}

TEST_CASE("render_numeric is deterministic and digit-dependent per cell", "[verifier][render]") {
    // The last fraction cell holds '0' for +00.000 and '1' for +00.001.
    // Distinct glyphs -> distinct lit-pixel counts in that one cell.
    const int   buses[1] = {1};
    const int   cell_x   = kValueX + 6 * kGlyphW;   // 7th glyph (last fraction digit)

    nt::reset_runtime();
    const float v0[1] = {0.000f};
    render_numeric(buses, v0, 1);
    int zero_cell  = lit_in_window(cell_x, 0, cell_x + kGlyphW, kRowH);

    nt::reset_runtime();
    render_numeric(buses, v0, 1);
    int zero_again = lit_in_window(cell_x, 0, cell_x + kGlyphW, kRowH);
    REQUIRE(zero_cell == zero_again);     // deterministic

    nt::reset_runtime();
    const float v1[1] = {0.001f};
    render_numeric(buses, v1, 1);
    int one_cell = lit_in_window(cell_x, 0, cell_x + kGlyphW, kRowH);
    REQUIRE(one_cell != zero_cell);       // '1' glyph differs from '0' glyph
    REQUIRE(one_cell > 0);
}

TEST_CASE("render_scope draws a trace within the screen", "[verifier][render]") {
    nt::reset_runtime();
    float buf[kScopeWidth];
    for (int i = 0; i < kScopeWidth; ++i)
        buf[i] = (i % 32 < 16) ? 1.0f : -1.0f;   // square-ish
    render_scope(buf, kScopeWidth, scope_trigger(buf, kScopeWidth), 5.0f);
    REQUIRE(lit_in_window(0, 0, 256, 64) > 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make build/host/test_verifier`
Expected: FAIL, `render_numeric` / `render_scope` / `volts_to_y` / `glyph_for` undefined.

- [ ] **Step 3: Write minimal implementation**

Add to `plugins/probes/verifier_logic.h` inside the namespace. The font is a 5-wide pattern in a 6 px cell, 7 rows in an 8 px cell; each row's low 5 bits are columns (bit 4 = leftmost). This table is the single source of truth the font-dump tool (Task 6) emits to font.json.

```cpp
// 6x8 bitmap font. Index by glyph char via glyph_for(); each entry is 8 rows,
// low 5 bits per row (bit 4 = leftmost column). Column 5 and row 7 are blank.
struct Glyph { uint8_t rows[8]; };

inline const Glyph& glyph_for(char c) {
    static const Glyph blank   = {{0,0,0,0,0,0,0,0}};
    static const Glyph digits[10] = {
        {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00}}, // 0
        {{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00}}, // 1
        {{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00}}, // 2
        {{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E,0x00}}, // 3
        {{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00}}, // 4
        {{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00}}, // 5
        {{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00}}, // 6
        {{0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00}}, // 7
        {{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00}}, // 8
        {{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00}}, // 9
    };
    static const Glyph plus    = {{0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00}};
    static const Glyph minus   = {{0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00}};
    static const Glyph dot     = {{0x00,0x00,0x00,0x00,0x00,0x06,0x06,0x00}};
    static const Glyph sentinel= {{0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == '+') return plus;
    if (c == '-') return minus;
    if (c == '.') return dot;
    if (c == '#') return sentinel;
    return blank;
}

inline void draw_glyph(int x0, int y0, char c) {
    const Glyph& g = glyph_for(c);
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 5; ++col)
            if (g.rows[row] & (1 << (4 - col)))
                NT_drawShapeI(kNT_point, x0 + col, y0 + row, x0 + col, y0 + row, 15);
}

inline int volts_to_y(float v, float vscale) {
    if (vscale <= 0.0f) vscale = 5.0f;
    float t = 0.5f - (v / (2.0f * vscale));   // +vscale -> top (0), -vscale -> bottom
    int y = (int)(t * 63.0f + 0.5f);
    if (y < 0) y = 0;
    if (y > 63) y = 63;
    return y;
}

inline void render_numeric(const int* buses, const float* values, int count) {
    char buf[8];
    for (int row = 0; row < count; ++row) {
        int y0 = row * kRowH;
        draw_glyph(0, y0, (char)('0' + (buses[row] / 10) % 10));
        draw_glyph(kGlyphW, y0, (char)('0' + (buses[row] % 10)));
        format_mv(volts_to_millivolts(values[row]), buf);
        for (int i = 0; i < kValueChars; ++i)
            draw_glyph(kValueX + i * kGlyphW, y0, buf[i]);
    }
}

inline void render_scope(const float* buf, int len, int trig, float vscale) {
    int prev_y = volts_to_y(buf[trig % len], vscale);
    for (int x = 1; x < len && x < 256; ++x) {
        int idx = (trig + x) % len;
        int y = volts_to_y(buf[idx], vscale);
        NT_drawShapeI(kNT_line, x - 1, prev_y, x, y);
        prev_y = y;
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make build/host/test_verifier && ./build/host/test_verifier '[render]'`
Expected: PASS, 3 cases. Then full suite `./build/host/test_verifier`, expect 12 cases.

- [ ] **Step 5: Commit**

```bash
git add plugins/probes/verifier_logic.h harness/tests/test_verifier.cpp
git commit -m "feat(verifier): numeric and scope render helpers"
```

---

### Task 5: The Verifier _NT_algorithm wrapper

**Files:**
- Create: `plugins/probes/Verifier.cpp`
- Modify: `Makefile` (ARM rule + `arm:` target)
- Modify: `harness/tests/test_verifier.cpp` (drive via `pluginEntry`)

- [ ] **Step 1: Write the failing test**

Append to `harness/tests/test_verifier.cpp`:

```cpp
extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data);

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
    static uint8_t sram[4096];
    static uint8_t dram[256];
    _NT_algorithmMemoryPtrs ptrs{};
    ptrs.sram = sram; ptrs.dram = dram; ptrs.dtc = nullptr; ptrs.itc = nullptr;
    _NT_algorithm* alg = f->construct(ptrs, req, specs);
    REQUIRE(alg != nullptr);

    // Param layout: see enum in Verifier.cpp. Set View=Numeric, First bus=13,
    // Count=1, Numeric mode=Mean.
    int16_t v[16] = {0};
    alg->v = v;
    v[kP_View]   = kNumeric;
    v[kP_First]  = 13;
    v[kP_Count]  = 1;
    v[kP_Mode]   = kMean;
    v[kP_Reset]  = 0;
    v[kP_ScopeBus] = 13;
    v[kP_Timebase] = 1;

    int nf = nt::bus_frame_count();
    float* b13 = nt::bus_pointer(13, nf);
    for (int i = 0; i < nf; ++i) b13[i] = 2.0f;

    f->step(alg, nt::bus_frames_base(), nf / 4);
    f->step(alg, nt::bus_frames_base(), nf / 4);

    // Two windows of 2.0 -> mean 2.0. Read it back via draw -> here we check the
    // exposed accessor instead (added in implementation).
    REQUIRE(verifier_mean_for_test(alg, 0) == Catch::Approx(2.0f));

    // Reset edge clears.
    v[kP_Reset] = kReset_On;
    f->parameterChanged(alg, kP_Reset);
    REQUIRE(verifier_mean_for_test(alg, 0) == Catch::Approx(0.0f));
}
```

Note: `kP_*`, `kReset_On`, and `verifier_mean_for_test` are defined in `Verifier.cpp` (next step) and declared at the top of the test via:

```cpp
// Declared by Verifier.cpp for host tests.
enum { kP_View, kP_First, kP_Count, kP_Mode, kP_Reset, kP_ScopeBus, kP_Timebase };
enum { kReset_Off = 0, kReset_On = 1 };
float verifier_mean_for_test(_NT_algorithm* self, int row);
```

Add these declarations near the top of the test file (after the includes).

- [ ] **Step 2: Run test to verify it fails**

Run: `make build/host/test_verifier`
Expected: FAIL, link error: `pluginEntry` / `verifier_mean_for_test` undefined (Verifier.cpp not yet linked).

- [ ] **Step 3: Write minimal implementation**

Create `plugins/probes/Verifier.cpp`:

```cpp
#include <distingnt/api.h>
#include <new>
#include "verifier_logic.h"

using namespace verifier;

enum { kP_View, kP_First, kP_Count, kP_Mode, kP_Reset, kP_ScopeBus, kP_Timebase };
enum { kReset_Off = 0, kReset_On = 1 };

struct _verifier : public _NT_algorithm {
    Reduction red[kMaxBuses];
    float     scope[kScopeWidth];
    int       scope_wr;
    int       scope_phase;
    bool      scope_filled;
};

static char const* const viewStrings[]  = { "Numeric", "Scope" };
static char const* const modeStrings[]  = { "Mean", "Min", "Max", "PkPk" };
static char const* const resetStrings[] = { "Off", "Reset" };

static const _NT_parameter parameters[] = {
    { .name = "View",     .min = 0, .max = 1,  .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = viewStrings },
    { .name = "First bus",.min = 1, .max = kNT_lastBus, .def = 13, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Count",    .min = 1, .max = kMaxBuses, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Mode",     .min = 0, .max = 3,  .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = modeStrings },
    { .name = "Reset",    .min = 0, .max = 1,  .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = resetStrings },
    { .name = "Scope bus",.min = 1, .max = kNT_lastBus, .def = 13, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Timebase", .min = 1, .max = 64, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
};
static const uint8_t page1[] = { kP_View, kP_First, kP_Count, kP_Mode, kP_Reset, kP_ScopeBus, kP_Timebase };
static const _NT_parameterPage pages[] = {
    { .name = "Verifier", .numParams = ARRAY_SIZE(page1), .params = page1 },
};
static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages), .pages = pages,
};

static void clear_accumulators(_verifier* a) {
    for (int i = 0; i < kMaxBuses; ++i) reduction_reset(a->red[i]);
    a->scope_wr = 0; a->scope_phase = 0; a->scope_filled = false;
}

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_verifier);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements&, const int32_t*) {
    auto* a = new (ptrs.sram) _verifier();
    a->parameters     = parameters;
    a->parameterPages = &parameterPages;
    clear_accumulators(a);
    return a;
}

void parameterChanged(_NT_algorithm* self, int p) {
    auto* a = (_verifier*)self;
    if (p == kP_Reset && a->v[kP_Reset] == kReset_On) clear_accumulators(a);
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a = (_verifier*)self;
    int numFrames = numFramesBy4 * 4;
    int first = a->v[kP_First];
    int count = a->v[kP_Count];
    if (count > kMaxBuses) count = kMaxBuses;
    for (int i = 0; i < count; ++i) {
        int bus = first + i;
        if (bus < 1 || bus > kNT_lastBus) continue;
        const float* in = busFrames + (bus - 1) * numFrames;
        reduction_accumulate(a->red[i], in, numFrames);
    }
    int sbus = a->v[kP_ScopeBus];
    if (sbus >= 1 && sbus <= kNT_lastBus) {
        const float* sin = busFrames + (sbus - 1) * numFrames;
        scope_push(a->scope, a->scope_wr, a->scope_phase, a->scope_filled,
                   sin, numFrames, a->v[kP_Timebase]);
    }
}

bool draw(_NT_algorithm* self) {
    auto* a = (_verifier*)self;
    if (a->v[kP_View] == kNumeric) {
        int count = a->v[kP_Count];
        if (count > kMaxBuses) count = kMaxBuses;
        int   buses[kMaxBuses];
        float values[kMaxBuses];
        for (int i = 0; i < count; ++i) {
            buses[i]  = a->v[kP_First] + i;
            values[i] = reduction_value(a->red[i], a->v[kP_Mode]);
        }
        render_numeric(buses, values, count);
    } else {
        int trig = scope_trigger(a->scope, kScopeWidth);
        render_scope(a->scope, kScopeWidth, trig, 5.0f);
    }
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('V','r','f','y'),
    .name = "Verifier",
    .description = "Reads any bus and renders a screenshot-parseable readout.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
};

extern "C" __attribute__((visibility("default"))) uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}

// Host-test accessor: the mean (mode-independent) of accumulator `row`.
float verifier_mean_for_test(_NT_algorithm* self, int row) {
    auto* a = (_verifier*)self;
    return reduction_value(a->red[row], verifier::kMean);
}
```

Update the test rule to link `Verifier.cpp`:

```makefile
build/host/test_verifier: harness/tests/test_verifier.cpp plugins/probes/Verifier.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^
```

Add the ARM build rule near the other probe rules (after `build/arm/bus_probe.o`):

```makefile
build/arm/Verifier.o: plugins/probes/Verifier.cpp plugins/probes/verifier_logic.h
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<
```

Add `build/arm/Verifier.o` to the `arm:` target list (the line beginning `arm: build/arm/gainCustomUI.o ...`):

```makefile
arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/Verifier.o build/arm/aeabi_probe.o build/arm/reentrancy_probe.o $(PILOT_APPLET_OBJS) $(HOST_PLUGIN_OBJS) $(OC_APP_OBJS)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make build/host/test_verifier && ./build/host/test_verifier '[wrap]'`
Expected: PASS, 2 cases.
Run: `make build/arm/Verifier.o`
Expected: compiles, produces `build/arm/Verifier.o`.

- [ ] **Step 5: Commit**

```bash
git add plugins/probes/Verifier.cpp harness/tests/test_verifier.cpp Makefile
git commit -m "feat(verifier): _NT_algorithm wrapper, params, ARM build"
```

---

### Task 6: Font-dump tool

**Files:**
- Create: `harness/tools/dump_font.cpp`
- Modify: `Makefile` (add `build/host/dump_font` rule)
- Create: `harness/verifier/fixtures/font.json` (generated)

The python parser needs Verifier's glyph bitmaps as templates. Verifier's `glyph_for` table (Task 4) is the single source of truth. This tool includes `verifier_logic.h` and emits each glyph's 6x8 cell (expanded from the 5-bit rows: column 5 and row 7 blank) to font.json. No rendering or screen readback is involved, so there is no nibble-order concern, and because Verifier draws exactly this table on both sim and hardware, the templates are hardware-valid.

- [ ] **Step 1: Write the tool**

Create `harness/tools/dump_font.cpp`:

```cpp
// Emits Verifier's own glyph table (verifier_logic.h glyph_for) as a JSON map
// of glyph -> 6x8 bitmap (row-major, 48 ints). Single source of truth shared
// with the device render. Stdout is redirected to font.json by the Makefile.
#include <cstdio>
#include "../../plugins/probes/verifier_logic.h"

using namespace verifier;

int main() {
    const char* glyphs = "0123456789+-.#";
    printf("{\n");
    for (const char* g = glyphs; *g; ++g) {
        const Glyph& gl = glyph_for(*g);
        printf("  \"%c\": [", *g);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 6; ++x) {
                int bit = (x < 5) ? ((gl.rows[y] >> (4 - x)) & 1) : 0;
                printf("%d", bit);
                if (!(y == 7 && x == 5)) printf(",");
            }
        }
        printf("]%s\n", g[1] ? "," : "");
    }
    printf("}\n");
    return 0;
}
```

- [ ] **Step 2: Add the Makefile rule and generate**

This tool needs no harness sources (pure header). Add near the host-test rules:

```makefile
build/host/dump_font: harness/tools/dump_font.cpp plugins/probes/verifier_logic.h
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tools/dump_font.cpp

harness/verifier/fixtures/font.json: build/host/dump_font
	mkdir -p harness/verifier/fixtures
	./build/host/dump_font > $@
```

Run: `make harness/verifier/fixtures/font.json && cat harness/verifier/fixtures/font.json`
Expected: a JSON object with keys `0`-`9`, `+`, `-`, `.`, `#`, each a 48-int array (6 wide, 8 tall, row-major). Each digit has at least one lit pixel; the four glyphs `0`,`1`,`8`,`#` are all distinct.

- [ ] **Step 3: Commit**

```bash
git add harness/tools/dump_font.cpp harness/verifier/fixtures/font.json Makefile
git commit -m "feat(verifier): font-dump tool emitting the glyph table"
```

---

### Task 7: Python parser, parse_numeric

**Files:**
- Create: `harness/verifier/__init__.py`
- Create: `harness/verifier/font.py`
- Create: `harness/verifier/parser.py`
- Create: `harness/verifier/tests/__init__.py`
- Create: `harness/verifier/tests/test_parser.py`
- Create: `requirements-dev.txt`

- [ ] **Step 1: Add pytest dev dependency**

Create `requirements-dev.txt`:

```text
pytest>=8.0
```

Run: `python3 -m pip install -r requirements-dev.txt`
Expected: pytest installed.

- [ ] **Step 2: Write the failing test**

Create `harness/verifier/__init__.py` (empty) and `harness/verifier/tests/__init__.py` (empty).

Create `harness/verifier/tests/test_parser.py`:

```python
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
```

- [ ] **Step 3: Run test to verify it fails**

Run: `python3 -m pytest harness/verifier/tests/test_parser.py -v`
Expected: FAIL, `harness.verifier.font` / `parser` import error.

- [ ] **Step 4: Write minimal implementation**

Create `harness/verifier/font.py`:

```python
"""Verifier's own font glyph templates, loaded from font.json.

font.json is emitted from the verifier_logic.h glyph table, which the device
draws verbatim on both sim and hardware, so these templates are hardware-valid.
"""
from __future__ import annotations

import json
from pathlib import Path

_FONT_PATH = Path(__file__).parent / "fixtures" / "font.json"

GLYPHS: dict[str, list[int]] = {
    k: list(v) for k, v in json.loads(_FONT_PATH.read_text()).items()
}
```

Create `harness/verifier/parser.py`:

```python
"""Parse the Verifier readout from a 256x64 grayscale screen array."""
from __future__ import annotations

from dataclasses import dataclass

from harness.verifier import font

GLYPH_W = 6
GLYPH_H = 8
VALUE_CHARS = 7  # sNN.fff


@dataclass(frozen=True)
class Layout:
    first_bus: int
    count: int
    value_x: int
    row_h: int
    row_y0: int


def _cell(screen: list[int], x0: int, y0: int) -> tuple[int, ...]:
    return tuple(
        1 if screen[(y0 + y) * 256 + (x0 + x)] else 0
        for y in range(GLYPH_H)
        for x in range(GLYPH_W)
    )


def _match_glyph(cell: tuple[int, ...]) -> str:
    best, best_score = "#", -1
    for ch, bmp in font.GLYPHS.items():
        score = sum(1 for a, b in zip(cell, bmp) if a == b)
        if score > best_score:
            best, best_score = ch, score
    return best


def _read_value(screen: list[int], x0: int, y0: int) -> float:
    chars = [
        _match_glyph(_cell(screen, x0 + i * GLYPH_W, y0))
        for i in range(VALUE_CHARS)
    ]
    text = "".join(chars)
    sign = -1.0 if text[0] == "-" else 1.0
    integer = int(text[1:3])
    frac = int(text[4:7])
    return sign * (integer + frac / 1000.0)


def parse_numeric(screen: list[int], layout: Layout) -> dict[int, float]:
    out: dict[int, float] = {}
    for row in range(layout.count):
        y0 = layout.row_y0 + row * layout.row_h
        out[layout.first_bus + row] = _read_value(screen, layout.value_x, y0)
    return out
```

- [ ] **Step 5: Run test to verify it passes**

Run: `python3 -m pytest harness/verifier/tests/test_parser.py -v`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add harness/verifier/__init__.py harness/verifier/font.py harness/verifier/parser.py harness/verifier/tests/__init__.py harness/verifier/tests/test_parser.py requirements-dev.txt
git commit -m "feat(verifier): python parse_numeric with font templates"
```

---

### Task 8: Python parser, parse_scope

**Files:**
- Modify: `harness/verifier/parser.py`
- Modify: `harness/verifier/tests/test_parser.py`

- [ ] **Step 1: Write the failing test**

Append to `harness/verifier/tests/test_parser.py`:

```python
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
    assert result.shape in {"square", "pulse"}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m pytest harness/verifier/tests/test_parser.py::test_parse_scope_estimates_frequency_and_shape -v`
Expected: FAIL, `ScopeRegion` / `parse_scope` undefined.

- [ ] **Step 3: Write minimal implementation**

Append to `harness/verifier/parser.py`:

```python
@dataclass(frozen=True)
class ScopeRegion:
    x0: int
    y0: int
    width: int
    height: int


@dataclass(frozen=True)
class ScopeResult:
    samples: tuple[float, ...]
    frequency_hz: float
    shape: str


def _column_centroid(screen: list[int], region: ScopeRegion, x: int) -> float | None:
    ys = [y for y in range(region.height)
          if screen[(region.y0 + y) * 256 + (region.x0 + x)]]
    if not ys:
        return None
    mid = sum(ys) / len(ys)
    # map pixel row to a normalized [-1, 1], row 0 top (+1), bottom (-1)
    return 1.0 - 2.0 * (mid / (region.height - 1))


def _classify(samples: tuple[float, ...]) -> str:
    hi = max(samples)
    lo = min(samples)
    span = hi - lo
    if span == 0:
        return "flat"
    near_rail = sum(1 for s in samples if s > hi - 0.1 * span or s < lo + 0.1 * span)
    if near_rail / len(samples) > 0.7:
        return "square"
    return "wave"


def parse_scope(screen: list[int], region: ScopeRegion,
                sample_rate: float, timebase: int) -> ScopeResult:
    cols = [_column_centroid(screen, region, x) for x in range(region.width)]
    samples = tuple(c for c in cols if c is not None)
    crossings = [
        x for x in range(1, len(cols))
        if cols[x - 1] is not None and cols[x] is not None
        and cols[x - 1] < 0 <= cols[x]
    ]
    if len(crossings) >= 2:
        period_px = (crossings[-1] - crossings[0]) / (len(crossings) - 1)
        frequency_hz = sample_rate / (period_px * timebase)
    else:
        frequency_hz = 0.0
    return ScopeResult(samples=samples, frequency_hz=frequency_hz,
                       shape=_classify(samples))
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m pytest harness/verifier/tests/test_parser.py -v`
Expected: PASS, 2 cases. The square test expects `shape in {"square", "pulse"}`; `_classify` returns `"square"`.

- [ ] **Step 5: Commit**

```bash
git add harness/verifier/parser.py harness/verifier/tests/test_parser.py
git commit -m "feat(verifier): python parse_scope shape and frequency"
```

---

### Task 9: Build wiring and full-suite verification

**Files:**
- Modify: `Makefile` (add a `test-verifier` convenience target)

- [ ] **Step 1: Add the convenience target**

Add near the other `test-*` phony targets:

```makefile
.PHONY: test-verifier
test-verifier: build/host/test_verifier
	./build/host/test_verifier
	python3 -m pytest harness/verifier/tests -q
```

- [ ] **Step 2: Run the full Verifier suite**

Run: `make test-verifier`
Expected: Catch2 all cases pass; pytest all cases pass.

- [ ] **Step 3: Confirm the ARM artifact and no regressions**

Run: `make build/arm/Verifier.o`
Expected: builds.
Run: `make test-applets`
Expected: existing applet tests still pass (no shared-surface regression; Verifier added only its own files plus additive Makefile lines).

- [ ] **Step 4: Inspect the ARM .text size (budget sanity)**

Run: `arm-none-eabi-size build/arm/Verifier.o`
Expected: `.text` well under the ~82 KB cap (expected 2-6 KB, kin to bus_probe).

- [ ] **Step 5: Commit**

```bash
git add Makefile
git commit -m "chore(verifier): test-verifier target and suite wiring"
```

---

## Deferred to the hardware session (not in this plan)

- Deploy `build/arm/Verifier.o` via `make deploy-sysex`, confirm it registers (GUID `Vrfy`) and ADDs to a preset.
- In-preset direct read: load a plug-in-under-test plus Verifier (Verifier after it in slot order), screenshot, parse, assert. (Sim coverage exists via bus injection; hardware confirms slot-order behavior.)
- Physical loopback: patch an output jack to a Verifier input jack, assert the analog path.
- Confirm the device renders the glyph table to the screen as expected (the font is repo-defined and identical sim/hardware by construction, so no font regeneration is expected; this is a transport sanity check, not a font-capture step).
- Confirm the bus-to-volts assumption (1.0f equals 1 V) by feeding a known DC level through `bus_probe`.

## Self-review

Spec coverage:

- Reductions (Mean/Min/Max/PkPk), latched, double sum: Task 1.
- Millivolt fixed-width formatting, no snprintf, overflow sentinel: Task 2.
- Scope decimation to a bounded 256-sample buffer, zero-cross trigger with fallback: Task 3.
- Numeric and scope render via a self-drawn 6x8 bitmap font and `NT_drawShapeI`: Task 4.
- `_NT_algorithm` wrapper, all SysEx params, Reset edge in parameterChanged, ARM build, GUID Vrfy: Task 5.
- Font templates emitted from the glyph table to font.json: Task 6.
- parse_numeric template match: Task 7. parse_scope shape and frequency with sample_rate plus timebase: Task 8.
- Build wiring (ARM rule, arm: target, host-test rule, pytest dep, convenience target): Tasks 1, 5, 9.
- Hazards: numParameters = ARRAY_SIZE (Task 5), no NT_setParameterFromUi (Task 5), no serialise (none added), bounded SRAM (Task 3/5), .text budget check (Task 9).

Type consistency: `Reduction`, `NumericMode`/`ViewMode` enums, `kP_*` param indices, `kReset_On`, `Layout`, `ScopeRegion`, `ScopeResult` are defined once and reused with the same names across tasks. The test-only declarations in Task 5 mirror the `Verifier.cpp` definitions exactly.

No placeholders: every code step shows complete content; every run step names the command and expected outcome.
