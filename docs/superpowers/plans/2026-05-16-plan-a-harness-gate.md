# Plan A: Harness Validation Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the host simulator, the hardware capture infrastructure, and validate bit-faithful parity between simulator and disting NT hardware for `examples/gainCustomUI.cpp` and `examples/gain.cpp`. This is the blocking gate from the spec; no shim work begins until it passes.

**Architecture:** Two compilation targets sharing one source tree. Host simulator (`harness/`) is a native binary that implements the NT API surface enough to run reference plug-ins from source. Hardware plug-ins are built with `arm-none-eabi-c++` and deployed to the NT. Three Stage-A helper plug-ins (`bus_probe`, `screen_dump`, `font_dump`) capture ground truth from the live module. The simulator is then iterated until it matches hardware bit-for-bit on audio, screen, and parameter-change paths.

**Tech Stack:** C++11, `arm-none-eabi-c++` (hardware), host clang/g++, GNU Make, Catch2 v3 single-header (host tests), Python 3 (scenario driver + diff util), git submodules (vendor pinning).

**Plan decomposition note.** This is Plan A of three. Plan B (shim core + Tier 1 applets) and Plan C (Tier 2 applets) follow after Plan A executes. The spec's "Open questions for user" (NT deployment mechanism, MIDI control-surface mapping) are resolved during Stage A.5; the answers feed back into Plan B.

**Revision-2 changelog (post-review):**

- Task 9: `LoadedPlugin` struct ownership locked to Task 9 (no "re-order if simpler" hedge). Task 10 contributes only `nt::load_plugin()` as a sibling resolver.
- Task 22: instrumentation regex now accepts unnamed parameters (`bool draw(_NT_algorithm*)`).

**Revision-1 changelog (post-review):**

- P2: PyYAML committed to in Task 13; hand-rolled subset removed.
- P3: Task 9 expanded to specify `NT_setParameterFromUi` semantics — writes to `v[p]` then invokes `parameterChanged(self, p)` on the loaded plug-in.
- P1: New Task 23b ("font verification") inserted between font capture (23) and Stage B (25). Asserts captured glyphs render byte-identical to hardware for a curated set, isolating font correctness from compositional drawing.
- P7: New Task 18a ("deploy mechanism verification") inserted before all hardware tasks. Tries USB MSC first, falls back to SD card / dedicated tool, documents what worked.
- New Task 0 ("bootstrap"): `bootstrap.sh` checks/installs all host-side deps so a fresh clone can `./bootstrap.sh && make host && make arm`.
- P4 (absorbed inline): each hardware task now lists explicit task dependencies in its header.
- P5 (absorbed inline): Task 26 iteration loop now requires recording (hypothesis, diff-before, diff-after, did-it-shrink). Abort if no shrink after one cycle (not after three iterations).
- P6 (absorbed inline): Task 24 expanded to probe one bus from each range (physical input 1, physical output 13, aux 21).
- Smaller: Task 11 main.cpp interface now enumerated (flags, stdin format, exit codes).
- Smaller: Task 22 instrumentation regex now permissive (handles `const`, named params, varied whitespace) and fails loudly on zero or multiple matches.

---

## Stage 0 — Repository scaffold

### Task 0: bootstrap.sh — verify and install host-side dependencies

**Files:**

- Create: `bootstrap.sh`
- Create: `requirements.txt`

The brief calls for reproducibility from a clean clone. `bootstrap.sh` is the documented entry point: it checks for required toolchain components, installs missing Python deps via pip, and surfaces any host-side gaps the engineer needs to resolve.

- [x] **Step 1: Write the failing test**

`bootstrap.sh` self-test: run it on a host with one dep missing (simulate by temporarily renaming `arm-none-eabi-c++`); script should exit non-zero with a clear message. Document the test sequence:

```bash
# happy path
./bootstrap.sh && echo OK

# unhappy path (manual simulation)
sudo mv $(which arm-none-eabi-c++) $(which arm-none-eabi-c++).bak
./bootstrap.sh   # expected: exit 1, names the missing tool
sudo mv $(which arm-none-eabi-c++).bak $(which arm-none-eabi-c++)
```

- [x] **Step 2: Implement bootstrap.sh**

```bash
#!/usr/bin/env bash
set -euo pipefail

REQUIRED_BINS=(git make python3 pip3 curl arm-none-eabi-c++)
MISSING=()
for bin in "${REQUIRED_BINS[@]}"; do
    if ! command -v "$bin" >/dev/null 2>&1; then
        MISSING+=("$bin")
    fi
done

# Host C++ compiler: either clang++ or g++ is acceptable
if ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
    MISSING+=("clang++ or g++")
fi

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "bootstrap: missing host-side tools:" >&2
    for m in "${MISSING[@]}"; do echo "  - $m" >&2; done
    echo "Install hints:" >&2
    echo "  macOS: brew install --cask gcc-arm-embedded; brew install python3" >&2
    echo "  Debian/Ubuntu: apt-get install gcc-arm-none-eabi python3 python3-pip curl" >&2
    exit 1
fi

# Python deps
pip3 install --user -r requirements.txt

# Submodules
git submodule update --init --recursive

# Verify Catch2 header presence (downloaded by Task 3 normally; idempotent here)
if [ ! -f harness/include/catch.hpp ]; then
    mkdir -p harness/include harness/src
    curl -sSL -o harness/include/catch.hpp \
        https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.hpp
    curl -sSL -o harness/src/catch_main.cpp \
        https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.cpp
fi

echo "bootstrap: OK"
```

- [x] **Step 3: Implement requirements.txt**

```text
PyYAML>=6.0
```

- [x] **Step 4: Run bootstrap.sh on a clean host**

Expected: prints `bootstrap: OK`, exit 0. Submodules populated.

- [x] **Step 5: Commit**

```bash
chmod +x bootstrap.sh
git add bootstrap.sh requirements.txt
git commit -m "chore: bootstrap.sh + requirements.txt for reproducible setup"
```

---

### Task 1: Top-level Makefile and directory skeleton

**Files:**

- Create: `Makefile`
- Create: `harness/.gitkeep`
- Create: `shim/.gitkeep`
- Create: `applets/.gitkeep`
- Create: `tests/scenarios/.gitkeep`
- Create: `tests/golden/.gitkeep`
- Create: `tests/reference/.gitkeep`
- Create: `vendor/.gitkeep`

- [x] **Step 1: Create top-level Makefile**

```makefile
# nt_and_crime top-level Makefile
.PHONY: help vendor host arm test clean deploy

ARM_CXX  := arm-none-eabi-c++
HOST_CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo g++)

NT_API_INCLUDE := vendor/distingNT_API/include
HEM_SRC_DIR    := vendor/O_C-Phazerville/software/src

ARM_FLAGS := -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
             -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall \
             -I$(NT_API_INCLUDE)

HOST_FLAGS := -std=c++11 -fno-rtti -fno-exceptions -Wall -O2 \
              -DNT_HEM_HOST_SIM=1 \
              -Iharness/include -I$(NT_API_INCLUDE)

help:
	@echo "make vendor   - fetch pinned upstream sources via submodules"
	@echo "make arm      - build all NT plug-ins under build/arm/"
	@echo "make host     - build host simulator at build/host/sim"
	@echo "make test     - run all scripted scenarios"
	@echo "make deploy   - copy build/arm/*.o to DEVICE (default: /Volumes/NT)"
	@echo "make clean    - remove build/"

vendor:
	git submodule update --init --recursive

clean:
	rm -rf build/
```

- [x] **Step 2: Create dir skeleton**

```bash
mkdir -p harness/include/distingnt harness/src \
         shim/include shim/src \
         applets \
         tests/scenarios tests/golden tests/reference \
         vendor
touch harness/.gitkeep shim/.gitkeep applets/.gitkeep \
      tests/scenarios/.gitkeep tests/golden/.gitkeep tests/reference/.gitkeep \
      vendor/.gitkeep
```

- [x] **Step 3: Verify make help runs**

Run: `make help`
Expected: lists the five targets above, exit 0.

- [x] **Step 4: Commit**

```bash
git add Makefile harness/.gitkeep shim/.gitkeep applets/.gitkeep \
        tests/scenarios/.gitkeep tests/golden/.gitkeep tests/reference/.gitkeep \
        vendor/.gitkeep
git commit -m "chore: top-level Makefile and dir skeleton"
```

---

### Task 2: Pin vendor sources as git submodules

**Files:**

- Create: `.gitmodules`
- Modify: `Makefile` (vendor target body already exists; we add a verify rule)

- [x] **Step 1: Add the two submodules**

```bash
git submodule add https://github.com/expertsleepersltd/distingNT_API.git vendor/distingNT_API
git submodule add -b phazerville https://github.com/djphazer/O_C-Phazerville.git vendor/O_C-Phazerville
```

- [x] **Step 2: Pin to specific SHAs**

```bash
cd vendor/distingNT_API
git checkout main
DISTING_SHA=$(git rev-parse HEAD)
cd ../O_C-Phazerville
git checkout phazerville
PHAZ_SHA=$(git rev-parse HEAD)
cd ../..
echo "distingNT_API @ $DISTING_SHA"
echo "O_C-Phazerville @ $PHAZ_SHA"
```

Record the two SHAs in the commit message of Step 4 so a reader can find them without `git submodule status`.

- [x] **Step 3: Sanity-check the expected files exist**

```bash
test -f vendor/distingNT_API/include/distingnt/api.h
test -f vendor/distingNT_API/examples/gain.cpp
test -f vendor/distingNT_API/examples/gainCustomUI.cpp
test -f vendor/O_C-Phazerville/software/src/HemisphereApplet.h
test -f vendor/O_C-Phazerville/software/src/HSUtils.h
test -f vendor/O_C-Phazerville/software/src/applets/Logic.h
echo OK
```

Expected: prints `OK`.

- [x] **Step 4: Commit**

```bash
git add .gitmodules vendor/distingNT_API vendor/O_C-Phazerville
git commit -m "chore: pin vendor submodules (distingNT_API @ <SHA>, O_C-Phazerville/phazerville @ <SHA>)"
```

---

## Stage A.1 — Host simulator skeleton (no plug-ins yet)

### Task 3: NT runtime stubs (BSS-allocated NT_screen, NT_globals)

**Files:**

- Create: `harness/include/nt_runtime.h`
- Create: `harness/src/nt_runtime.cpp`
- Create: `harness/tests/test_nt_runtime.cpp`
- Modify: `Makefile` (add `host` target)
- Create: `harness/include/catch.hpp` (vendor the Catch2 v3 amalgamated header)

- [x] **Step 1: Vendor Catch2 v3 amalgamated header**

```bash
curl -sL https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.hpp \
  -o harness/include/catch.hpp
curl -sL https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.cpp \
  -o harness/src/catch_main.cpp
```

- [x] **Step 2: Write the failing test**

`harness/tests/test_nt_runtime.cpp`:

```cpp
// Catch2 v3 amalgamated: catch_main.cpp provides int main; do not define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>

TEST_CASE("NT_screen is exactly 128*64 bytes and initially zero", "[runtime]") {
    REQUIRE(sizeof(NT_screen) == 128 * 64);
    nt::reset_runtime();
    for (size_t i = 0; i < 128 * 64; ++i) {
        REQUIRE(NT_screen[i] == 0);
    }
}

TEST_CASE("NT_globals is initialised with sane defaults", "[runtime]") {
    nt::reset_runtime();
    REQUIRE(NT_globals.sampleRate == 48000u);
    REQUIRE(NT_globals.maxFramesPerStep == 64u);
    REQUIRE(NT_globals.workBuffer != nullptr);
    REQUIRE(NT_globals.workBufferSizeBytes >= 64u * 1024u);
}
```

- [x] **Step 3: Run the test and confirm it fails to link**

Add to `Makefile`:

```makefile
build/host/test_nt_runtime: harness/tests/test_nt_runtime.cpp harness/src/nt_runtime.cpp harness/src/catch_main.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

test-runtime: build/host/test_nt_runtime
	./build/host/test_nt_runtime
```

Run: `make test-runtime`
Expected: link error, `nt_runtime.h` not found.

- [x] **Step 4: Implement minimal runtime**

`harness/include/nt_runtime.h`:

```cpp
#pragma once
#include <cstdint>
#include <distingnt/api.h>

namespace nt {
void reset_runtime();
}
```

`harness/src/nt_runtime.cpp`:

```cpp
#include "nt_runtime.h"
#include <cstring>
#include <cstdint>

uint8_t NT_screen[128 * 64];

static float    g_work_buffer[64 * 1024 / sizeof(float)];
const _NT_globals NT_globals = {
    .sampleRate           = 48000u,
    .maxFramesPerStep     = 64u,
    .workBuffer           = g_work_buffer,
    .workBufferSizeBytes  = sizeof(g_work_buffer),
    .streamSizeBytes      = 0u,
    .streamBufferSizeBytes= 0u,
};

namespace nt {
void reset_runtime() {
    std::memset(NT_screen, 0, sizeof(NT_screen));
}
}
```

- [x] **Step 5: Run the test and confirm it passes**

Run: `make test-runtime`
Expected: exit 0; Catch2 reports passing test cases (assertion count includes the per-byte loop in the zero-check test, so the printed total is in the thousands, not 2).

- [x] **Step 6: Commit**

```bash
git add harness/ Makefile
git commit -m "feat(harness): NT_screen and NT_globals BSS stubs with reset_runtime()"
```

---

### Task 4: Bus frame storage and parameter access stubs

**Files:**

- Modify: `harness/include/nt_runtime.h`
- Modify: `harness/src/nt_runtime.cpp`
- Create: `harness/tests/test_buses.cpp`

- [x] **Step 1: Write the failing test**

`harness/tests/test_buses.cpp`:

```cpp
#include "catch.hpp"
#include "nt_runtime.h"

TEST_CASE("bus frame layout matches api.h convention", "[runtime]") {
    nt::reset_runtime();
    REQUIRE(nt::num_buses() == 64);
    REQUIRE(nt::bus_frame_count() > 0);

    float* in1 = nt::bus_pointer(1, 32);
    float* in2 = nt::bus_pointer(2, 32);
    REQUIRE(in2 - in1 == 32);  // contiguous, bus 1 then bus 2

    *in1 = 0.5f;
    REQUIRE(nt::bus_pointer(1, 32)[0] == Approx(0.5f));
}

TEST_CASE("bus 0 is the unmapped sentinel", "[runtime]") {
    REQUIRE(nt::bus_pointer(0, 32) == nullptr);
}
```

- [x] **Step 2: Run the test and confirm it fails**

Add to `Makefile`:

```makefile
build/host/test_buses: harness/tests/test_buses.cpp harness/src/nt_runtime.cpp harness/src/catch_main.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

test-buses: build/host/test_buses
	./build/host/test_buses
```

Run: `make test-buses`
Expected: compile error, `nt::num_buses` etc. not defined.

- [x] **Step 3: Implement bus storage**

Add to `harness/include/nt_runtime.h`:

```cpp
namespace nt {
int   num_buses();
int   bus_frame_count();
void  set_bus_frame_count(int frames);
float* bus_pointer(int bus_index_1_based, int numFrames);
float* bus_frames_base();
}
```

Add to `harness/src/nt_runtime.cpp`:

```cpp
#include <vector>

static std::vector<float> g_bus_storage;
static int                g_bus_frames = 32;

namespace nt {
int  num_buses()        { return 64; }
int  bus_frame_count()  { return g_bus_frames; }
void set_bus_frame_count(int frames) {
    g_bus_frames = frames;
    g_bus_storage.assign((size_t)num_buses() * (size_t)frames, 0.0f);
}
float* bus_pointer(int bus_index, int numFrames) {
    if (bus_index <= 0 || bus_index > num_buses()) return nullptr;
    if ((int)g_bus_storage.size() < num_buses() * numFrames)
        set_bus_frame_count(numFrames);
    return &g_bus_storage[(size_t)(bus_index - 1) * (size_t)numFrames];
}
float* bus_frames_base() {
    return g_bus_storage.empty() ? nullptr : &g_bus_storage[0];
}
}
```

Also extend `nt::reset_runtime()` to call `set_bus_frame_count(32)`.

- [x] **Step 4: Run the test and confirm it passes**

Run: `make test-buses`
Expected: `All tests passed`.

- [x] **Step 5: Commit**

```bash
git add harness/ Makefile
git commit -m "feat(harness): bus frame storage with bus 0 = unmapped sentinel"
```

---

### Task 5: Drawing API — NT_drawText with deterministic 6x8 ASCII font

**Files:**

- Modify: `harness/include/nt_runtime.h`
- Modify: `harness/src/nt_runtime.cpp`
- Create: `harness/src/font_placeholder.cpp` (placeholder until Stage A.4 captures real font)
- Create: `harness/tests/test_draw_text.cpp`

- [x] **Step 1: Write the failing test**

`harness/tests/test_draw_text.cpp`:

```cpp
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
```

- [x] **Step 2: Run the test and confirm it fails**

Run after extending the Makefile with a `test-draw` target analogous to prior tasks.
Expected: link error, `NT_drawText` not defined.

- [x] **Step 3: Implement placeholder 6x8 font and NT_drawText**

`harness/src/font_placeholder.cpp`:

```cpp
#include <cstdint>
// Placeholder font: every glyph is a solid 6x8 rectangle.
// Replaced after Stage A.4 captures the real NT firmware font.
namespace nt {
const uint8_t* font_6x8_glyph(char c) {
    static const uint8_t solid[6] = {0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e};
    static const uint8_t blank[6] = {0,0,0,0,0,0};
    if (c < 32 || c > 126) return blank;
    return solid;
}
}
```

Add to `harness/src/nt_runtime.cpp`:

```cpp
namespace nt { extern const uint8_t* font_6x8_glyph(char c); }

static inline void set_pixel(int x, int y, int colour) {
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
    int byte_index  = y * 128 + (x >> 1);
    uint8_t mask    = (x & 1) ? 0xf0 : 0x0f;
    uint8_t shifted = (uint8_t)((colour & 0x0f) << ((x & 1) ? 4 : 0));
    NT_screen[byte_index] = (uint8_t)((NT_screen[byte_index] & ~mask) | shifted);
}

extern "C" void NT_drawText(int x, int y, const char* str, int colour,
                            _NT_textAlignment align, _NT_textSize size) {
    (void)align; (void)size;  // alignment/size handled in a later task
    if (!str) return;
    int cx = x;
    for (; *str; ++str) {
        const uint8_t* g = nt::font_6x8_glyph(*str);
        for (int col = 0; col < 6; ++col) {
            uint8_t bits = g[col];
            for (int row = 0; row < 8; ++row) {
                if (bits & (1u << row))
                    set_pixel(cx + col, y + row, colour);
            }
        }
        cx += 6;
    }
}
```

- [x] **Step 4: Run the test and confirm it passes**

Run: `make test-draw`
Expected: `All tests passed`.

- [x] **Step 5: Commit**

```bash
git add harness/ Makefile
git commit -m "feat(harness): NT_drawText with placeholder 6x8 font; replaced post-A.4"
```

---

### Task 6: Drawing API — NT_drawShapeI (integer, non-AA)

**Files:**

- Modify: `harness/src/nt_runtime.cpp`
- Create: `harness/tests/test_draw_shape.cpp`

- [x] **Step 1: Write the failing test**

`harness/tests/test_draw_shape.cpp`:

```cpp
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
```

- [x] **Step 2: Run the test and confirm it fails**

Expected: link error, `NT_drawShapeI` not defined.

- [x] **Step 3: Implement NT_drawShapeI**

```cpp
extern "C" void NT_drawShapeI(_NT_shape shape, int x0, int y0, int x1, int y1, int colour) {
    switch (shape) {
    case kNT_point: set_pixel(x0, y0, colour); break;
    case kNT_line: {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            set_pixel(x0, y0, colour);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    } break;
    case kNT_box: {
        NT_drawShapeI(kNT_line, x0, y0, x1, y0, colour);
        NT_drawShapeI(kNT_line, x1, y0, x1, y1, colour);
        NT_drawShapeI(kNT_line, x1, y1, x0, y1, colour);
        NT_drawShapeI(kNT_line, x0, y1, x0, y0, colour);
    } break;
    case kNT_rectangle: {
        int lo_x = std::min(x0, x1), hi_x = std::max(x0, x1);
        int lo_y = std::min(y0, y1), hi_y = std::max(y0, y1);
        for (int y = lo_y; y <= hi_y; ++y)
            for (int x = lo_x; x <= hi_x; ++x)
                set_pixel(x, y, colour);
    } break;
    case kNT_circle: {
        int r = (int)std::sqrt((double)((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0)));
        int x = r, y = 0, err = 0;
        while (x >= y) {
            set_pixel(x0 + x, y0 + y, colour);
            set_pixel(x0 + y, y0 + x, colour);
            set_pixel(x0 - y, y0 + x, colour);
            set_pixel(x0 - x, y0 + y, colour);
            set_pixel(x0 - x, y0 - y, colour);
            set_pixel(x0 - y, y0 - x, colour);
            set_pixel(x0 + y, y0 - x, colour);
            set_pixel(x0 + x, y0 - y, colour);
            ++y;
            if (err <= 0) { err += 2*y + 1; }
            else          { --x; err -= 2*x + 1; }
        }
    } break;
    }
}
```

Add `<cmath>` and `<algorithm>` includes if not already present.

- [x] **Step 4: Run the test and confirm it passes**

Run: `make test-draw-shape`
Expected: `All tests passed`.

- [x] **Step 5: Commit**

```bash
git add harness/ Makefile
git commit -m "feat(harness): NT_drawShapeI line/box/rect/circle (Bresenham)"
```

---

### Task 7: Drawing API — NT_drawShapeF (antialiased) placeholder + flag

**Files:**

- Modify: `harness/src/nt_runtime.cpp`

The antialiased rasteriser is empirically derived against hardware in Stage A.4. For now, implement a non-AA float-cast wrapper around `NT_drawShapeI`. Track that this is a placeholder via a runtime flag `nt::shape_rasteriser_is_placeholder()` returning `true`; flip to `false` once the rasteriser is replaced.

- [x] **Step 1: Write the failing test**

```cpp
TEST_CASE("NT_drawShapeF is currently a placeholder", "[draw]") {
    REQUIRE(nt::shape_rasteriser_is_placeholder());
}
TEST_CASE("NT_drawShapeF degenerates to NT_drawShapeI behaviour for integer coords", "[draw]") {
    nt::reset_runtime();
    NT_drawShapeF(kNT_line, 0.0f, 0.0f, 10.0f, 0.0f, 15.0f);
    for (int x = 0; x <= 10; ++x) REQUIRE(pixel(x, 0) == 15);
}
```

- [x] **Step 2: Implement and verify pass**

Add to `harness/include/nt_runtime.h`:

```cpp
namespace nt { bool shape_rasteriser_is_placeholder(); }
```

Add to `harness/src/nt_runtime.cpp`:

```cpp
extern "C" void NT_drawShapeF(_NT_shape shape, float x0, float y0, float x1, float y1, float colour) {
    NT_drawShapeI(shape, (int)x0, (int)y0, (int)x1, (int)y1, (int)colour);
}
namespace nt { bool shape_rasteriser_is_placeholder() { return true; } }
```

- [x] **Step 3: Commit**

```bash
git add harness/
git commit -m "feat(harness): NT_drawShapeF placeholder (non-AA, replaced post-A.4)"
```

---

### Task 8: JSON stream/parse stubs for serialise/deserialise

**Files:**

- Create: `harness/include/distingnt/serialisation.h`
- Create: `harness/src/nt_jsonstream.cpp`
- Create: `harness/tests/test_json.cpp`

The NT API ships `distingnt/serialisation.h` declaring `class _NT_jsonStream` and `class _NT_jsonParse`. Provide a host-compatible implementation that backs onto a `std::string` buffer.

- [x] **Step 1: Read the upstream header and mirror its public surface**

Reference: `vendor/distingNT_API/include/distingnt/serialisation.h`. Copy or `#include` directly (preferred — single source of truth). Update the harness's include path so plug-ins find the same header at the same path on both targets.

- [x] **Step 2: Write the failing test**

`harness/tests/test_json.cpp`:

```cpp
#include "catch.hpp"
#include "nt_jsonstream.h"  // host-only helper that exposes the implementation
#include <distingnt/serialisation.h>
#include <cstring>

TEST_CASE("serialise roundtrip: simple object", "[json]") {
    auto stream = nt::make_json_stream();
    stream->openObject();
    stream->addMemberName("answer");
    stream->addNumber(42);
    stream->addMemberName("name");
    stream->addString("banana");
    stream->closeObject();

    auto parse = nt::make_json_parse(stream->buffer());
    int n_members = 0;
    REQUIRE(parse->numberOfObjectMembers(n_members));
    REQUIRE(n_members == 2);
    REQUIRE(parse->matchName("answer"));
    int v = 0;
    REQUIRE(parse->number(v));
    REQUIRE(v == 42);
    REQUIRE(parse->matchName("name"));
    const char* s = nullptr;
    REQUIRE(parse->string(s));
    REQUIRE(strcmp(s, "banana") == 0);
}
```

- [x] **Step 3: Implement using a writer + a tokenising parser**

Implementation is mechanical JSON read/write over `std::string`. The host-only helper `nt::make_json_stream()` returns a unique_ptr to a concrete subclass of `_NT_jsonStream`; same for parse. Buffer-backed.

Expected line count: ~250 LoC. Decompose into helpers if it grows past 300.

- [x] **Step 4: Run the test and confirm it passes**

- [x] **Step 5: Commit**

```bash
git add harness/
git commit -m "feat(harness): _NT_jsonStream and _NT_jsonParse host-side impl"
```

---

### Task 9: Parameter & algorithm slot stubs

**Files:**

- Modify: `harness/include/nt_runtime.h`
- Modify: `harness/src/nt_runtime.cpp`
- Create: `harness/tests/test_params.cpp`

The harness hosts exactly one algorithm slot for the validation gate. Implement `NT_algorithmIndex`, `NT_algorithmCount`, `NT_parameterOffset`, `NT_setParameterFromUi`, `NT_setParameterFromAudio`, `NT_setParameterGrayedOut`.

**Routing semantics (specified explicitly because Task 10 and Task 27 depend on this):**

`NT_setParameterFromUi(algIdx, paramIdx, value)` performs exactly these steps in order:

1. Look up the algorithm by `algIdx` (the single slot the harness hosts).
2. Bounds-check `paramIdx - NT_parameterOffset()` against the algorithm's `parameters[]` table. Out-of-bounds → silently no-op (matches NT firmware's documented forgiveness).
3. Write `value` into the algorithm's `v[paramIdx - NT_parameterOffset()]`. (`v` is mutable from inside the harness; on hardware the firmware owns it. Cast away const in the harness implementation; document the cast.)
4. Invoke the algorithm's `factory->parameterChanged(self, paramIdx - NT_parameterOffset())`.

`NT_setParameterFromAudio` is identical except the doc says it's safe to call from audio-rate callbacks; in the host harness this distinction is moot (no thread separation), but the routing is the same.

`NT_setParameterGrayedOut(algIdx, paramIdx, bool)` records the gray state in a per-parameter side table; it does not call `parameterChanged`. The state is queryable via `nt::is_parameter_grayed_out(algIdx, paramIdx)` for test assertions.

- [x] **Step 1: Write the failing tests (three assertions)**

```cpp
TEST_CASE("NT_setParameterFromUi writes v[p] and calls parameterChanged", "[params]") {
    nt::reset_runtime();
    auto* alg = nt::load_test_algorithm();
    int triggers_before = nt::test_parameter_changed_count();
    int p = 0;
    NT_setParameterFromUi(NT_algorithmIndex(alg->algorithm), p + NT_parameterOffset(), 75);

    REQUIRE(alg->algorithm->v[p] == 75);                    // step 3: value written
    REQUIRE(nt::test_parameter_changed_count() == triggers_before + 1);   // step 4: invoked
    REQUIRE(nt::test_last_changed_param() == p);            // step 4: with correct index
}

TEST_CASE("NT_setParameterFromUi with out-of-bounds param is a no-op", "[params]") {
    nt::reset_runtime();
    auto* alg = nt::load_test_algorithm();
    int triggers_before = nt::test_parameter_changed_count();
    NT_setParameterFromUi(NT_algorithmIndex(alg->algorithm), 999 + NT_parameterOffset(), 1);
    REQUIRE(nt::test_parameter_changed_count() == triggers_before);
}

TEST_CASE("NT_setParameterGrayedOut records state without invoking parameterChanged", "[params]") {
    nt::reset_runtime();
    auto* alg = nt::load_test_algorithm();
    int triggers_before = nt::test_parameter_changed_count();
    NT_setParameterGrayedOut(NT_algorithmIndex(alg->algorithm), 0 + NT_parameterOffset(), true);
    REQUIRE(nt::is_parameter_grayed_out(NT_algorithmIndex(alg->algorithm), 0 + NT_parameterOffset()));
    REQUIRE(nt::test_parameter_changed_count() == triggers_before);
}
```

`nt::load_test_algorithm` returns a `nt::LoadedPlugin` (defined in Task 10 — forward declared here) wrapping a stub algorithm with one parameter and a global counter that increments on every `parameterChanged`.

- [x] **Step 2: Implement, verify, commit**

Implementation decision (locked, not "re-order if simpler"): the `LoadedPlugin` struct and the loader scaffold are introduced **here in Task 9**, not in Task 10. Task 9 defines:

```cpp
// harness/include/plugin_loader.h
namespace nt {
struct LoadedPlugin {
    const _NT_factory* factory;
    _NT_algorithm*     algorithm;
};
LoadedPlugin* load_test_algorithm();  // for parameter-routing tests
}
```

Task 10's only additional contribution is the public `nt::load_plugin()` entry point that resolves the single statically linked plug-in (rather than the in-test stub). Both functions return `LoadedPlugin*`; both go through the same internal resolver. The Task 9 test code uses `alg->algorithm` (the `_NT_algorithm*` inside `LoadedPlugin`) — see the test snippets above.

```bash
git add harness/
git commit -m "feat(harness): NT_setParameter* with explicit routing semantics"
```

---

### Task 10: Plugin loader (static link)

**Files:**

- Create: `harness/include/plugin_loader.h`
- Create: `harness/src/plugin_loader.cpp`
- Create: `harness/tests/test_loader.cpp`

The harness loads plug-ins by static-linking their `.cpp` file directly with `harness/src/*.cpp`. The loader's job is to call `pluginEntry()` and unpack the factory.

- [x] **Step 1: Write the failing test**

```cpp
TEST_CASE("loader can resolve a plugin's factory and call construct", "[loader]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();  // resolves the single statically linked plug-in
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    REQUIRE(loaded->factory->guid != 0);
}
```

- [x] **Step 2: Implement and verify**

`nt::load_plugin()` calls `pluginEntry(kNT_selector_factoryInfo, 0)`, then `calculateRequirements`, then `construct` with malloc'd memory regions. Stores the factory and constructed algorithm in a struct.

- [x] **Step 3: Commit**

```bash
git add harness/
git commit -m "feat(harness): static-link plugin loader with factory unpack"
```

---

## Stage A.2 — Build reference plug-ins for both targets

### Task 11: Build gainCustomUI.cpp for the host

**Files:**

- Modify: `Makefile`

- [x] **Step 1: Add host-build target for the simulator binary linked against gainCustomUI**

```makefile
HARNESS_SRCS := harness/src/nt_runtime.cpp \
                harness/src/font_placeholder.cpp \
                harness/src/nt_jsonstream.cpp \
                harness/src/plugin_loader.cpp \
                harness/src/catch_main.cpp

build/host/sim_gainCustomUI: $(HARNESS_SRCS) vendor/distingNT_API/examples/gainCustomUI.cpp harness/src/main.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

host: build/host/sim_gainCustomUI
```

- [x] **Step 2: Specify `harness/src/main.cpp` interface**

The binary is a single-shot scenario runner driven from the command line. Interface:

```text
usage: sim_<plugin> --scenario PATH [--output-dir DIR] [--write-golden] [--quiet]

flags:
  --scenario PATH    YAML scenario file (required)
  --output-dir DIR   where to write out_bus.bin, out_screen.bin, out_params.log
                     (default: ./out/)
  --write-golden     overwrite tests/golden/<plugin>/<scenario-name>/ with the
                     simulator's output. Use to bootstrap a golden master.
  --quiet            suppress per-step progress output

stdin:  unused
stdout: progress lines like "frame 256: pot C -> 0.75"
exit codes:
  0  scenario completed; outputs written
  1  scenario failed to parse
  2  plugin construction failed
  3  scenario runtime error (e.g. out-of-bounds param)
```

`main.cpp` is a thin shell: parse args, read the scenario via `harness/scripts/run_scenario.py` *no* — `main.cpp` parses YAML itself via the Python driver invoking the binary with flags. Simpler split: the Python driver parses YAML, the C++ binary receives parsed scenario as JSON on stdin (or via a temp file). This decouples C++ from YAML parsing entirely.

Revised flow:

1. `harness/scripts/run_scenario.py` reads YAML, converts to JSON, invokes the C++ binary with `--scenario-json -` and pipes JSON on stdin.
2. C++ binary parses JSON via `harness/src/nt_jsonstream.cpp` (already vendored for the serialisation surface).
3. C++ binary runs the scenario, writes outputs to `--output-dir`.
4. Python driver diffs outputs against golden, exits with combined status.

This keeps YAML out of the C++ build entirely. Update Step 1's `--scenario` to `--scenario-json` and remove YAML parsing from main.cpp.

- [x] **Step 3: Smoke test — link and run with --help**

```bash
make host && ./build/host/sim_gainCustomUI --help
```

Expected: prints usage block above, exit 0.

- [ ] **Step 3: Commit**

```bash
git add Makefile harness/src/main.cpp
git commit -m "build: host simulator binary linked against gainCustomUI"
```

---

### Task 12: Build gainCustomUI.cpp for ARM target

**Files:**

- Modify: `Makefile`

- [x] **Step 1: Add ARM rule**

```makefile
ARM_REF_SRCS := vendor/distingNT_API/examples/gainCustomUI.cpp \
                vendor/distingNT_API/examples/gain.cpp

build/arm/%.o: vendor/distingNT_API/examples/%.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<

arm: build/arm/gainCustomUI.o build/arm/gain.o
```

- [x] **Step 2: Verify toolchain present**

```bash
which arm-none-eabi-c++ || echo "ARM toolchain missing"
```

If missing, install via `brew install --cask gcc-arm-embedded` (macOS) or distro equivalent. Required for hardware deploy.

- [x] **Step 3: Build**

```bash
make arm
```

Expected: `build/arm/gainCustomUI.o` and `build/arm/gain.o` exist, no warnings under `-Wall`.

- [x] **Step 4: Commit**

```bash
git add Makefile
git commit -m "build: ARM .o output for gainCustomUI and gain"
```

---

## Stage A.3 — Scenario driver

### Task 13: Scenario file format and YAML parser

**Files:**

- Create: `harness/scripts/run_scenario.py`
- Create: `tests/scenarios/gainCustomUI/zero_signal.yaml`

Choose YAML for human readability. Parser uses PyYAML (`import yaml`, already installed by `bootstrap.sh` via `requirements.txt`). Hand-rolling a YAML subset was rejected as a footgun: future scenarios will inevitably hit edge cases the hand-roller misses, and the diff between sim and golden will silently mask the parse error as a test failure.

**Scenario shape:**

```yaml
plugin: gainCustomUI
params:
  Input: 1
  Output: 13
  Gain: 50
input_buses:
  1: silence  # or sine:440:0.5, ramp:0:1, square:1:0.5
duration_frames: 1024
events:
  - { at_frame: 256, kind: pot,     pot: C, value: 0.75 }
  - { at_frame: 512, kind: encoder, side: L, delta: -1 }
expect:
  bus_out: tests/golden/gainCustomUI/zero_signal/out_bus.bin
  screen:  tests/golden/gainCustomUI/zero_signal/out_screen.bin
  params:  tests/golden/gainCustomUI/zero_signal/out_params.log
```

- [x] **Step 1: Write minimal scenario file**

`tests/scenarios/gainCustomUI/zero_signal.yaml`:

```yaml
plugin: gainCustomUI
params:
  Input: 1
  Output: 13
  Gain: 0
input_buses:
  1: silence
duration_frames: 64
events: []
expect:
  bus_out: tests/golden/gainCustomUI/zero_signal/out_bus.bin
```

- [x] **Step 2: Write Python driver that parses scenario and drives the sim binary**

`harness/scripts/run_scenario.py`:

```python
#!/usr/bin/env python3
"""Drive a host-simulator binary through a YAML scenario; diff outputs vs golden."""
import argparse
import json
import subprocess
import sys
import yaml
from pathlib import Path

def load_scenario(path: Path) -> dict:
    return yaml.safe_load(path.read_text())

def run_simulator(binary: Path, scenario_json: str, output_dir: Path) -> int:
    output_dir.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        [str(binary), "--scenario-json", "-", "--output-dir", str(output_dir)],
        input=scenario_json,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
    return proc.returncode

def diff_or_write_golden(actual_dir: Path, scenario: dict, write_golden: bool) -> int:
    expected = scenario.get("expect", {})
    rc = 0
    for kind, golden_path in expected.items():
        actual = actual_dir / f"out_{kind}.bin"
        golden = Path(golden_path)
        if write_golden:
            golden.parent.mkdir(parents=True, exist_ok=True)
            golden.write_bytes(actual.read_bytes())
            print(f"wrote golden: {golden}")
        else:
            r = subprocess.run(["python3", "harness/scripts/diff_outputs.py",
                               kind.replace("_", "-"), str(golden), str(actual)])
            if r.returncode != 0:
                rc = r.returncode
    return rc

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario", type=Path)
    parser.add_argument("--binary", default=None, type=Path,
                       help="path to sim binary; default derived from scenario.plugin")
    parser.add_argument("--write-golden", action="store_true")
    parser.add_argument("--output-dir", default=Path("out/"), type=Path)
    args = parser.parse_args()

    scenario = load_scenario(args.scenario)
    binary = args.binary or Path(f"build/host/sim_{scenario['plugin']}")
    rc = run_simulator(binary, json.dumps(scenario), args.output_dir)
    if rc != 0:
        return rc
    return diff_or_write_golden(args.output_dir, scenario, args.write_golden)

if __name__ == "__main__":
    sys.exit(main())
```

Full implementation in the commit; decompose helpers as needed.

- [x] **Step 3: Add Makefile target**

```makefile
test:
	python3 harness/scripts/run_scenario.py tests/scenarios/gainCustomUI/zero_signal.yaml
```

- [x] **Step 4: Generate the first golden master**

```bash
python3 harness/scripts/run_scenario.py \
    tests/scenarios/gainCustomUI/zero_signal.yaml --write-golden
```

This writes `tests/golden/gainCustomUI/zero_signal/out_bus.bin` from the simulator's output. Inspect manually (it should be all zeros for the zero-signal scenario).

- [x] **Step 5: Re-run and confirm clean diff**

```bash
make test
```

Expected: scenario passes (zero diff).

- [x] **Step 6: Commit**

```bash
git add harness/scripts/ tests/scenarios/ tests/golden/
git commit -m "feat(harness): scenario driver and zero_signal golden master"
```

---

### Task 14: Diff utility for `out_bus.bin` / `out_screen.bin` / `out_params.log`

**Files:**

- Create: `harness/scripts/diff_outputs.py`

- [x] **Step 1: Implement**

```python
#!/usr/bin/env python3
"""Bit-faithful diff for binary outputs; tolerant diff for audio with --lsb-tolerance N."""
import argparse, sys
from pathlib import Path

def diff_binary(a, b, lsb_tolerance=0):
    ...

def diff_screen(a, b):
    # exact byte match; visualise mismatches in a tiny PGM
    ...

def diff_params(a, b):
    # textual diff
    ...

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=["bus", "screen", "params"])
    parser.add_argument("expected")
    parser.add_argument("actual")
    parser.add_argument("--lsb-tolerance", type=int, default=0)
    args = parser.parse_args()
    ...
```

- [x] **Step 2: Smoke-test against two identical files**

Expected: exit 0, no output.

- [x] **Step 3: Smoke-test against two differing files**

Expected: exit 1, prints first divergence with byte offset.

- [x] **Step 4: Commit**

```bash
git add harness/scripts/diff_outputs.py
git commit -m "feat(harness): bit-faithful diff util with optional LSB tolerance"
```

---

## Stage A.4 — Hardware capture plug-ins

### Task 15: bus_probe.cpp

**Files:**

- Create: `applets/bus_probe.cpp` (plain NT plug-in, not shim-based)

- [ ] **Step 1: Implement**

```cpp
#include <distingnt/api.h>
#include <new>
#include <cstring>

struct _busProbe : public _NT_algorithm {
    int targetBus;
    float testLevel;
};

enum { kParamBus, kParamLevel };
static const _NT_parameter parameters[] = {
    { .name = "Bus",   .min = 1, .max = kNT_lastBus, .def = 1,
      .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Level", .min = 0, .max = 100, .def = 50,
      .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
};
static const uint8_t page1[] = { kParamBus, kParamLevel };
static const _NT_parameterPage pages[] = {
    { .name = "Probe", .numParams = ARRAY_SIZE(page1), .params = page1 },
};
static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages), .pages = pages,
};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_busProbe);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                        const _NT_algorithmRequirements&, const int32_t*) {
    auto* alg = new (ptrs.sram) _busProbe();
    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    alg->targetBus      = 1;
    alg->testLevel      = 0.5f;
    return alg;
}

void parameterChanged(_NT_algorithm* self, int p) {
    auto* a = (_busProbe*)self;
    if (p == kParamBus)   a->targetBus = a->v[kParamBus];
    if (p == kParamLevel) a->testLevel = a->v[kParamLevel] / 100.0f;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a = (_busProbe*)self;
    int numFrames = numFramesBy4 * 4;
    float* out = busFrames + (a->targetBus - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) out[i] = a->testLevel;
}

bool draw(_NT_algorithm* self) {
    auto* a = (_busProbe*)self;
    char buf[32];
    int len = NT_intToString(buf, a->targetBus);
    buf[len] = 0;
    NT_drawText(0, 30, "Probe bus:");
    NT_drawText(80, 30, buf);
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('P','r','o','B'),
    .name = "Bus probe",
    .description = "Writes a known level onto the selected bus.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:     return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
```

- [ ] **Step 2: Add Makefile rule**

```makefile
build/arm/bus_probe.o: applets/bus_probe.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<
```

Add `build/arm/bus_probe.o` to the `arm` target's prerequisites.

- [ ] **Step 3: Build**

Run: `make arm`
Expected: clean build, no warnings.

- [ ] **Step 4: Commit**

```bash
git add applets/bus_probe.cpp Makefile
git commit -m "feat(applets): bus_probe.cpp for Stage A bus enumeration"
```

---

### Task 16: hem_dump_helper.h and screen_dump.cpp

**Files:**

- Create: `shim/include/hem_dump_helper.h`
- Create: `applets/screen_dump.cpp`

`hem_dump_helper.h` is the header-only static-inline that both `screen_dump.cpp` and any Plan-B-instrumented reference plug-in include.

- [ ] **Step 1: Implement the helper**

```cpp
#pragma once
#include <distingnt/api.h>
#include <cstdint>
#include <cstring>

namespace nt_hem {

static uint8_t g_capture[128 * 64];
static bool    g_pending = false;

static inline void _nt_hem_dump_screen() {
    std::memcpy(g_capture, NT_screen, sizeof(g_capture));
    g_pending = true;
}

constexpr uint8_t kManufacturerId = 0x7D;  // educational/private use byte
constexpr uint8_t kCmdDumpRequest = 0x01;
constexpr uint8_t kCmdDumpReply   = 0x02;

static inline void emit_capture_if_pending(uint32_t destination = kNT_destinationUSB) {
    if (!g_pending) return;
    // Emit in 256-byte payload chunks with a 4-byte header per chunk:
    //   [manuf_id, kCmdDumpReply, seq, total]
    constexpr uint32_t kChunkBytes = 256;
    const uint32_t total = (sizeof(g_capture) + kChunkBytes - 1) / kChunkBytes;
    uint8_t header[4] = { kManufacturerId, kCmdDumpReply, 0, (uint8_t)total };
    for (uint32_t s = 0; s < total; ++s) {
        header[2] = (uint8_t)s;
        NT_sendMidiSysEx(destination, header, sizeof(header), false);
        const uint32_t offset = s * kChunkBytes;
        const uint32_t this_chunk =
            (offset + kChunkBytes <= sizeof(g_capture)) ? kChunkBytes
                                                        : sizeof(g_capture) - offset;
        NT_sendMidiSysEx(destination, g_capture + offset, this_chunk, s == total - 1);
    }
    g_pending = false;
}

} // namespace nt_hem
```

- [ ] **Step 2: Implement screen_dump.cpp**

```cpp
#include <distingnt/api.h>
#include <new>
#include "hem_dump_helper.h"

struct _screenDump : public _NT_algorithm { };

static const _NT_parameter parameters[] = {};
static const _NT_parameterPages parameterPages = { .numPages = 0, .pages = nullptr };

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = 0;
    req.sram = sizeof(_screenDump);
}
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                        const _NT_algorithmRequirements&, const int32_t*) {
    auto* alg = new (ptrs.sram) _screenDump();
    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm*, float*, int) {}

bool draw(_NT_algorithm*) {
    // Capture whatever the prior slot left in NT_screen.
    nt_hem::_nt_hem_dump_screen();
    return true;  // suppress default param line
}

void midiSysEx(const uint8_t* message, uint32_t count) {
    if (count >= 2 && message[0] == nt_hem::kManufacturerId
                   && message[1] == nt_hem::kCmdDumpRequest) {
        nt_hem::emit_capture_if_pending();
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('D','m','p','S'),
    .name = "Screen dump",
    .description = "Co-load in slot 2; emits NT_screen as SysEx on request.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = nullptr,
    .step = step,
    .draw = draw,
    .midiSysEx = midiSysEx,
    .tags = kNT_tagUtility,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:     return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
```

- [ ] **Step 3: Add to Makefile and build**

```bash
make arm
```

Expected: `build/arm/screen_dump.o` clean build.

- [ ] **Step 4: Commit**

```bash
git add shim/include/hem_dump_helper.h applets/screen_dump.cpp Makefile
git commit -m "feat(applets): screen_dump.cpp + hem_dump_helper.h"
```

---

### Task 17: font_dump.cpp

**Files:**

- Create: `applets/font_dump.cpp`

Algorithm: every frame, advance one glyph index, clear the screen, call `NT_drawText` for that glyph at a known position, then `_nt_hem_dump_screen()` to capture. Loop through all 95 printable ASCII glyphs per font size. The harness watches the SysEx stream and decodes per-glyph.

- [ ] **Step 1: Implement**

```cpp
#include <distingnt/api.h>
#include <new>
#include <cstring>
#include "hem_dump_helper.h"

struct _fontDump : public _NT_algorithm {
    int      glyph_index;  // 0..94
    int      font_size;    // 0,1,2 -> tiny,normal,large
    uint32_t frame_counter;
};

static const _NT_parameter parameters[] = {
    { .name = "Size", .min = 0, .max = 2, .def = 1,
      .unit = kNT_unitEnum, .scaling = 0, .enumStrings = (const char*[]){ "tiny", "normal", "large" } },
};
static const uint8_t page1[] = { 0 };
static const _NT_parameterPage pages[] = {
    { .name = "FontDump", .numParams = 1, .params = page1 },
};
static const _NT_parameterPages parameterPages = { .numPages = 1, .pages = pages };

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_fontDump);
}
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                        const _NT_algorithmRequirements&, const int32_t*) {
    auto* alg = new (ptrs.sram) _fontDump();
    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    alg->glyph_index = 0;
    alg->font_size   = 1;
    return alg;
}

void parameterChanged(_NT_algorithm* self, int) {
    auto* a = (_fontDump*)self;
    a->font_size    = a->v[0];
    a->glyph_index  = 0;
}

void step(_NT_algorithm* self, float*, int) {
    auto* a = (_fontDump*)self;
    ++a->frame_counter;
}

bool draw(_NT_algorithm* self) {
    auto* a = (_fontDump*)self;
    std::memset(NT_screen, 0, sizeof(NT_screen));
    char ch = (char)(32 + a->glyph_index);
    char str[2] = { ch, 0 };
    _NT_textSize sz = (a->font_size == 0) ? kNT_textTiny
                    : (a->font_size == 1) ? kNT_textNormal : kNT_textLarge;
    NT_drawText(0, 0, str, 15, kNT_textLeft, sz);
    nt_hem::_nt_hem_dump_screen();
    a->glyph_index = (a->glyph_index + 1) % 95;
    return true;
}

void midiSysEx(const uint8_t* message, uint32_t count) {
    if (count >= 2 && message[0] == nt_hem::kManufacturerId
                   && message[1] == nt_hem::kCmdDumpRequest) {
        nt_hem::emit_capture_if_pending();
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('D','m','p','F'),
    .name = "Font dump",
    .description = "Iterates ASCII glyphs and exposes them via screen_dump SysEx.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .midiSysEx = midiSysEx,
    .tags = kNT_tagUtility,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:     return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
```

- [ ] **Step 2: Build and commit**

```bash
make arm
git add applets/font_dump.cpp Makefile
git commit -m "feat(applets): font_dump.cpp iterating printable ASCII"
```

---

## Stage A.5 — Hardware deploy and Stage-A assumption verification

This stage is partially manual: it touches physical hardware. The plan documents the exact steps and acceptance criteria; the engineer runs them.

### Task 18: Deployment script (USB MSC default)

**Files:**

- Modify: `Makefile`
- Create: `harness/scripts/deploy.sh`

- [ ] **Step 1: Add deploy target**

Until the actual mechanism is verified by Task 18a, assume USB MSC:

```makefile
DEVICE ?= /Volumes/NT
deploy: arm
	@test -d "$(DEVICE)" || { echo "DEVICE=$(DEVICE) not mounted"; exit 1; }
	cp build/arm/*.o $(DEVICE)/plugins/
	@echo "Deployed to $(DEVICE)/plugins/. Power-cycle the NT to pick up new plug-ins."
```

- [ ] **Step 2: Commit**

```bash
git add Makefile harness/scripts/deploy.sh
git commit -m "feat(deploy): make deploy target (USB MSC default; verified in Task 18a)"
```

---

### Task 18a: Verify deploy mechanism on actual hardware

**Depends on:** Task 18 committed, NT module connected.

**Files:**

- Modify: `Makefile` (only if USB MSC is not the answer)
- Create: `docs/hardware-deploy.md`

This task answers spec Open Question 1. Every subsequent hardware task (19-28) assumes `make deploy` works; if it doesn't, every downstream task is blocked.

- [ ] **Step 1: Try USB MSC**

Plug the NT into the host machine. Check if it mounts as a USB Mass Storage device:

```bash
ls /Volumes/         # macOS
ls /media/$USER/     # Linux
```

If a volume appears that contains a `plugins/` directory, USB MSC is the answer. Note the mount path.

- [ ] **Step 2: Run deploy with a single .o**

```bash
make arm
make deploy DEVICE=<discovered-path>
```

Power-cycle the NT. Verify the plug-in appears in the "add algorithm" menu on the NT's UI.

- [ ] **Step 3: If USB MSC fails — fallback A: SD card**

Eject the NT, remove its SD card, mount on the host. Copy `build/arm/*.o` to `plugins/` on the SD card. Reinsert. Power-cycle.

If this works, update `Makefile` `deploy` target:

```makefile
DEVICE ?= /Volumes/NT_SD
deploy: arm
	@test -d "$(DEVICE)/plugins" || { echo "DEVICE=$(DEVICE) does not contain plugins/"; exit 1; }
	cp build/arm/*.o $(DEVICE)/plugins/
	@echo "Copied to $(DEVICE). Reinsert SD card and power-cycle."
```

- [ ] **Step 4: If both fail — fallback B: dedicated tool**

Consult the disting NT manual / Expert Sleepers community resources for the official plug-in deploy method. Whatever it is (custom CLI, web uploader, MIDI SysEx), wrap it in `harness/scripts/deploy.sh` and have the `Makefile` `deploy` target call the script.

- [ ] **Step 5: Document what worked**

Write `docs/hardware-deploy.md` capturing:

- Confirmed deploy mechanism (USB MSC / SD / tool).
- Exact mount path or tool invocation.
- Time-to-deploy: how long from `make deploy` to plug-in available on NT.
- Any quirks: does the NT need a power cycle, or does it hot-reload?

- [ ] **Step 6: Commit**

```bash
git add Makefile harness/scripts/deploy.sh docs/hardware-deploy.md
git commit -m "feat(deploy): confirmed deploy mechanism (<USB MSC|SD|tool>)"
```

If neither fallback works, abort: this is a project-blocking Open Question with no answer.

---

### Task 19: Stage A.1 — Verify bus count and routing on hardware

**Depends on:** Task 15 (`bus_probe.o` built), Task 18a (deploy mechanism confirmed).

**Hardware procedure (manual, agent waits for human confirmation):**

- [ ] **Step 1:** Load `bus_probe` in slot 1 of a fresh preset. Set Bus = 1, Level = 50%.
- [ ] **Step 2:** With an oscilloscope or audio interface, verify physical input/output bus 1 outputs +0.5 V (or the platform's per-volt convention; cross-reference Stage A.6 below).
- [ ] **Step 3:** Iterate Bus over 1..64. Record which bus indices produce no signal — these are aux buses not routed to physical connectors.
- [ ] **Step 4:** Write the bus map to `tests/reference/bus_map.txt`.
- [ ] **Step 5:** Commit:

```bash
git add tests/reference/bus_map.txt
git commit -m "test(reference): hardware bus map for Stage A.1"
```

If `kNT_lastBus == 64` does not match observed accessible buses, raise this as a finding before proceeding.

---

### Task 20: Stage A.2 — Verify screen_dump round-trip integrity

**Depends on:** Task 16 (`screen_dump.o` built), Task 18a (deploy mechanism confirmed), Task 19 (deploy + load mechanics validated via `bus_probe`).

- [ ] **Step 1:** Load `screen_dump` alone in slot 1 of a preset.
- [ ] **Step 2:** Send a SysEx test pattern of 256 bytes to the NT via USB MIDI: `F0 7D 01 ... F7` where `...` is the test payload. The harness's `screen_dump` shouldn't respond to this exact opcode (it expects `01`), but the test verifies the NT accepts inbound SysEx of that size.
- [ ] **Step 3:** Adjust the test: send `F0 7D 01 F7` (dump request). Expect a multi-message reply totalling 8192 + framing bytes.
- [ ] **Step 4:** Reassemble in `harness/scripts/sysex_receive.py`. Verify the reply is exactly `g_capture` from the prior frame.

If SysEx fails (drops, corrupts, times out), **abort A1**.

```bash
git add tests/reference/sysex_roundtrip.log
git commit -m "test(reference): SysEx roundtrip integrity verified"
```

---

### Task 21: Stage A.3 — Verify slot-order draw

**Depends on:** Tasks 19 and 20 both passed.

- [ ] **Step 1:** Load `bus_probe` in slot 1, `screen_dump` in slot 2. `bus_probe`'s `draw()` writes "Probe bus: N" to NT_screen.
- [ ] **Step 2:** Send dump-request. Capture screen via `screen_dump`.
- [ ] **Step 3:** Diff captured screen against the simulator's prediction for `bus_probe.draw()`.

Pass criterion: captured screen contains the text rendered by `bus_probe.draw()`, not zeroes.

If captured screen is empty, the host clears `NT_screen` between algorithm draw calls. Fall back to **Plan B** (Task 22).

```bash
git add tests/reference/slot_order_check.txt
git commit -m "test(reference): slot-order draw verified"
```

---

### Task 22 (conditional): Stage A.3-Plan-B — Instrumented reference plug-in

Only execute if Task 21 fails.

**Depends on:** Task 21 failed.

**Files:**

- Create: `harness/scripts/instrument_plugin.py`
- Modify: `Makefile`

- [ ] **Step 1:** Python script that copies `vendor/distingNT_API/examples/<name>.cpp` to `build/instrumented/<name>.cpp`, prepends `#include "hem_dump_helper.h"`, and injects `nt_hem::_nt_hem_dump_screen();` immediately before the closing `}` of the `draw()` function.

The match must accommodate real-world signature variations. Use a permissive regex over the function signature:

```python
import re
DRAW_SIGNATURE = re.compile(
    r"""^\s*                                       # leading whitespace
        bool\s+draw\s*\(\s*                        # 'bool draw(' with flexible whitespace
        (?:const\s+)?                              # optional const
        _NT_algorithm\s*\*\s*                      # _NT_algorithm*
        (?:[A-Za-z_][A-Za-z0-9_]*)?                # OPTIONAL parameter name
        \s*\)\s*                                   # close paren
        (?:const\s*)?                              # optional trailing const
        \{                                         # opening brace
    """,
    re.MULTILINE | re.VERBOSE,
)
```

The parameter-name group is now optional so `bool draw(_NT_algorithm*)` (unnamed parameter) matches. Both reference plug-ins name the parameter (`self`), but a future fixture might not.

The script must:

- Match exactly once. Zero matches → fail loudly: `"could not locate draw() in <file>"`. Two or more matches → fail loudly: `"ambiguous draw() match (N occurrences) in <file>"`. Either case exits non-zero so the build halts.
- After matching, locate the corresponding closing `}` by tracking brace depth (don't just regex it — `draw()` may contain nested braces).
- Inject `nt_hem::_nt_hem_dump_screen();\n` immediately before the closing `}`.
- [ ] **Step 2:** Makefile rule `build/arm/<name>_instrumented.o` that runs the instrumenter then compiles.
- [ ] **Step 3:** Re-run Task 21 with the instrumented gainCustomUI in slot 1, screen_dump in slot 2.
- [ ] **Step 4:** Commit:

```bash
git add harness/scripts/instrument_plugin.py Makefile
git commit -m "feat(harness): Plan-B source-rewrite instrumenter for reference plug-ins"
```

If Plan B also fails, **abort A1**.

---

### Task 23: Stage A.4 — Capture firmware font tables

**Depends on:** Task 17 (`font_dump.o` built), Task 20 (SysEx roundtrip), Task 21 or Task 22 (capture path).

- [ ] **Step 1:** Load `font_dump` in slot 1, `screen_dump` in slot 2.
- [ ] **Step 2:** For each font size (tiny, normal, large), iterate through all 95 ASCII glyphs, capturing each.
- [ ] **Step 3:** Parse captures into glyph tables stored as `tests/reference/nt_fonts/{tiny,normal,large}.bin`.
- [ ] **Step 4:** Replace `harness/src/font_placeholder.cpp` with `harness/src/font_captured.cpp` that consumes the captured tables.

```bash
git add tests/reference/nt_fonts/ harness/src/font_captured.cpp
git rm harness/src/font_placeholder.cpp
git commit -m "feat(harness): replace placeholder font with hardware-captured glyphs"
```

- [ ] **Step 5:** Re-run all draw tests. Expect zero pixel diffs against fresh hardware captures of identical scenes.

---

### Task 23b: Font verification (isolation test before Stage B)

**Depends on:** Task 23 complete.

This task isolates font correctness from compositional drawing. If the captured font has subtly wrong glyph metrics (wrong baseline, wrong advance, antialias differences in large-font), the failure shows up here cleanly rather than as a confusing cascade in Stage B's parity tests.

**Files:**

- Create: `tests/scenarios/font_verify/all_glyphs.yaml`
- Create: `tests/golden/font_verify/all_glyphs/`
- Modify: `Makefile` (`test` target depends on font verification)

- [ ] **Step 1: Build a host-side test plug-in `applets/font_verify.cpp`**

Identical interface to `font_dump.cpp` but built for both targets. It iterates printable ASCII glyphs and renders each into a known screen region. Used both on hardware (paired with `screen_dump`) and on the host (rendered into `NT_screen` directly).

- [ ] **Step 2: Capture hardware reference**

Deploy `font_verify` + `screen_dump`, capture all 95 × 3 = 285 glyphs into `tests/golden/font_verify/all_glyphs/`.

- [ ] **Step 3: Run host simulator producing the same scenario output**

The simulator uses `harness/src/font_captured.cpp` (the table from Task 23). The output should be byte-identical to hardware.

- [ ] **Step 4: Diff with zero tolerance**

```bash
python3 harness/scripts/run_scenario.py tests/scenarios/font_verify/all_glyphs.yaml
```

Expected: zero divergence across all 285 glyph captures.

If any glyph differs:

- For tiny/normal (1-bit pixels): the captured glyph table or the simulator's `set_pixel` is wrong. The diff visualisation (`harness/scripts/diff_outputs.py screen ...`) names the specific glyph and pixel coordinates.
- For large (4-bit antialiased): the simulator's `NT_drawText` with `kNT_textLarge` is using a wrong rasteriser. Treat as a Task-26-class divergence and apply the same shrink-rule iteration discipline (see Task 26).

- [ ] **Step 5: Commit**

```bash
git add applets/font_verify.cpp tests/scenarios/font_verify/ tests/golden/font_verify/ harness/
git commit -m "test(font): byte-faithful font verification across all printable ASCII"
```

Until this passes with zero divergence, Stage B (which depends on `NT_drawText`) is blocked.

---

### Task 24: Stage A.5 — Verify CV scaling on hardware (multi-bus)

**Depends on:** Task 19 (bus map), Task 18a (deploy).

CV scaling could in principle differ between bus ranges (physical input 1-12, physical output 13-20, aux 21-64). The shim and applets need scaling correctness on whichever buses they end up routed to, not just bus 13.

- [ ] **Step 1: Build `cv_calibrate.cpp`**

```cpp
// applets/cv_calibrate.cpp — writes a configured level to a configured bus.
// One parameter "Bus" (1..64), one parameter "Level" (volts, -10.0..10.0).
// Deployed as a hardware test fixture only.
```

Implementation mirrors `bus_probe.cpp` from Task 15 but writes `level` as a float (not as a fraction).

- [ ] **Step 2: Test bus 1 (physical input range)**

Set Bus=1, Level=1.0. Measure bus 1's physical connector with a multimeter. Record exact reading. Repeat for Level = 0.0, -1.0, 2.0, 5.0.

- [ ] **Step 3: Test bus 13 (physical output range)**

Same procedure on bus 13. Expected: same V-per-unit as bus 1.

- [ ] **Step 4: Test bus 21 (aux range)**

Aux buses aren't routed to a physical connector. Test by loading a second algorithm (any audio plug-in) on a different slot that consumes bus 21, and check whether the consumed signal matches the level written. If aux buses aren't accessible at all from a plug-in, document that and skip.

- [ ] **Step 5: Record findings**

`tests/reference/cv_scaling.txt`:

```text
bus  level_set  level_measured  notes
1    1.000      X.XXX           physical input
1    2.000      X.XXX
1    5.000      X.XXX
13   1.000      X.XXX           physical output
13   2.000      X.XXX
21   1.000      X.XXX           aux, measured via consumer plug-in
```

If `level_measured` ≠ `level_set` to within ±0.001 V on any bus, **the spec's CV scaling rule (1.0f = 1V) is wrong for that bus range and the shim's mapping must adjust** before Plan B begins. If aux buses cannot be probed, note the limitation and proceed; the shim only maps applet CV to physical outputs by default.

- [ ] **Step 6: Commit**

```bash
git add applets/cv_calibrate.cpp tests/reference/cv_scaling.txt
git commit -m "test(reference): CV scaling verified on bus ranges 1/13/21"
```

---

## Stage B — gainCustomUI parity

### Task 25: Audio path parity

**Depends on:** Task 12 (gainCustomUI built for ARM), Task 18a (deploy mechanism), Task 24 (CV scaling verified).

- [ ] **Step 1:** Define scenario `tests/scenarios/gainCustomUI/sine_50pct.yaml`: sine 440 Hz at 0.5 amplitude on bus 1, Gain = 50, Output bus = 13, 1024 frames.
- [ ] **Step 2:** Run on the simulator, capture `out_bus.bin`. Store under `tests/golden/gainCustomUI/sine_50pct/`.
- [ ] **Step 3:** Replay the same scenario on hardware: send sine via audio interface into NT physical bus 1, record bus 13 output via loopback. Save as `tests/hardware/gainCustomUI/sine_50pct/out_bus.bin`.
- [ ] **Step 4:** Diff with `--lsb-tolerance 1`. Expect zero divergence within tolerance.

If divergence > 1 LSB anywhere, debug:

- Sample rate mismatch (host = 48000, NT = ?). Verify and adjust `NT_globals.sampleRate`.
- Output mode (add vs replace). Verify the scenario's `Output mode` value matches both targets.
- Bus indexing (1-based vs 0-based). Cross-reference Task 19.

- [ ] **Step 5:** Commit:

```bash
git add tests/
git commit -m "test(gainCustomUI): sine_50pct audio parity within 1 LSB"
```

---

### Task 26: Screen path parity

**Depends on:** Task 23b (font verified in isolation), Task 25 (audio parity working).

- [ ] **Step 1:** Same scenario as Task 25 but capture `out_screen.bin` from both targets (sim via direct buffer read; hardware via `screen_dump` SysEx).
- [ ] **Step 2:** Diff bit-faithfully. Zero tolerance.
- [ ] **Step 3:** If divergence, run the shrink-discipline iteration loop.

**Iteration discipline (also applies to Task 23b for the large font, and any future bit-faithful divergence task):**

For each iteration cycle:

1. **Hypothesis.** State in one sentence what specific simulator component is wrong. Example: "NT_drawShapeF circle rasteriser uses Bresenham; hardware uses a midpoint-with-AA-correction."
2. **Diff-before.** Record the current divergence: how many pixels diverge, in which regions, by what magnitude. Save as `tests/divergence/<scenario>/iter-N-before.log`.
3. **Change.** Make the targeted simulator change.
4. **Diff-after.** Re-run the scenario, record the new divergence as `tests/divergence/<scenario>/iter-N-after.log`.
5. **Did the diff shrink?** Compare diff-before vs diff-after. If divergent pixel count went down or magnitude went down, this hypothesis class is on the right track; continue with refinements. If neither went down (or one went down while the other went up), the hypothesis class is wrong — stop refining this hypothesis.

Abort condition: if any single iteration produces no shrink, **abort A1 on that scenario**. Three iterations of no shrink is not better than one; the failure mode is the hypothesis class, not the cycle count.

- [ ] **Step 4:** Commit when zero diff achieved:

```bash
git add tests/ harness/
git commit -m "test(gainCustomUI): screen parity byte-faithful"
```

---

### Task 27: Custom UI parity

**Depends on:** Task 26 (screen path parity), and spec Open Question 2 answered (MIDI control-surface transport for UI event replay).

If Open Question 2 has no good answer (the NT does not expose a control-surface MIDI mapping for pots/buttons), build a small `applets/ui_inject.cpp` plug-in that consumes inbound SysEx commands of the shape `{manuf_id, opcode, control_id, value}` and synthesises a `_NT_uiData` for the target plug-in. Co-load it like `screen_dump`. Document in `docs/hardware-notes.md`.

- [ ] **Step 1:** Define scenario `tests/scenarios/gainCustomUI/ui_events.yaml`: scripted pot+encoder events that drive the Gain parameter from 50 to 75 to 0.
- [ ] **Step 2:** Simulator: drive customUi events, log every `parameterChanged(p, value)` call to `out_params.log`.
- [ ] **Step 3:** Hardware: replay the same UI events. Capture parameter values via SysEx queries (Open Question 2 — verify the mechanism in Task 19's notes).
- [ ] **Step 4:** Diff `out_params.log`. Zero tolerance.
- [ ] **Step 5:** Commit:

```bash
git add tests/
git commit -m "test(gainCustomUI): custom UI parity byte-faithful"
```

---

## Stage C — gain.cpp parity

### Task 28: gain.cpp parity (audio only)

**Depends on:** Task 25 (gainCustomUI audio parity passed; same audio path is reused here).

`gain.cpp` has no custom UI; this verifies audio-path validation works without UI coupling.

- [ ] **Step 1:** Scenarios mirror Task 25 with `plugin: gain` instead of `gainCustomUI`. Three scenarios: zero_signal, sine_50pct, sine_100pct.
- [ ] **Step 2:** Run both targets, diff with `--lsb-tolerance 1`. Zero divergence expected.
- [ ] **Step 3:** Commit:

```bash
git add tests/
git commit -m "test(gain): audio parity across three signal levels"
```

---

## Done criteria for Plan A

All of the following hold:

- `./bootstrap.sh && make arm && make host && make test` exits zero from a clean clone.
- Task 18a confirmed the deploy mechanism; `make deploy` works.
- All Stage A.5 hardware verifications complete with recorded results in `tests/reference/`:
  - `tests/reference/bus_map.txt` (Task 19)
  - `tests/reference/sysex_roundtrip.log` (Task 20)
  - `tests/reference/slot_order_check.txt` (Task 21)
  - `tests/reference/nt_fonts/{tiny,normal,large}.bin` (Task 23)
  - `tests/reference/cv_scaling.txt` (Task 24, with three bus ranges probed)
- Task 23b font verification passes byte-faithfully across all 285 glyphs (95 × 3 sizes).
- Stage B and Stage C scenarios pass with zero divergence (screen, params) and ±1 LSB tolerance (audio).
- Open Question 1 (deployment mechanism) and Open Question 2 (UI event transport) have concrete answers documented in `docs/hardware-notes.md`.
- No `*placeholder*.cpp` files remain in `harness/src/`.

When Plan A is done, signal readiness for Plan B (shim core + Tier 1 applets).

---

## Self-review (revision 2)

- **Spec coverage:** Stage 0 covers bootstrap, repo scaffold and submodule pinning. Stage A.1 covers Host simulator (NT_screen, NT_globals, drawing, JSON, params, loader) per spec's Host simulator section. Stages A.4/A.5 cover hardware capture infrastructure including the four Stage A assumptions (slot order, persistence, SysEx round-trip, throughput) per spec's Hardware screen capture section. Plan B for instrumented reference plug-ins is a conditional task (22). Task 23b isolates font correctness. Task 24 covers CV scaling across three bus ranges (P6). Stages B and C cover the harness-validation gate per spec.
- **Placeholder scan:** Two `...` ellipses remain inside Python helper scripts (Task 14 diff helpers). These are not specification gaps — the surrounding text describes the function's purpose and the engineer implements the body. Task 8 (`nt_jsonstream`) explicitly leaves the LoC budget flexible because the shape is mechanical and has multiple reasonable forms. No other placeholders.
- **Type consistency:** `_NT_factory` field names cross-referenced against api.h. `nt::reset_runtime`, `nt::num_buses`, `nt::bus_pointer`, `nt::load_test_algorithm`, `nt::load_plugin`, `nt::is_parameter_grayed_out`, `nt::test_parameter_changed_count`, `nt::test_last_changed_param`, `nt_hem::_nt_hem_dump_screen`, `nt_hem::emit_capture_if_pending`, `nt_hem::kManufacturerId`, `nt_hem::kCmdDumpRequest`, `nt_hem::kCmdDumpReply` all consistent across tasks.
- **Spec Open Questions:** Task 18a (deployment) and Task 27's preamble (UI event transport via `ui_inject.cpp` fallback) provide concrete paths and abort criteria.
- **Iteration discipline:** Task 26 codifies shrink-rule iteration (P5). Same rule referenced from Task 23b.
