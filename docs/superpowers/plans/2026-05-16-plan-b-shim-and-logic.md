# Plan B: Shim Core + Logic Applet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get the Phazerville `Logic.h` applet, copied byte-for-byte from upstream, compiling against a new shim and running as a disting NT plug-in with working gate inputs, gate outputs, encoder UI, and `View()` drawing.

**Architecture:** Build a minimal shim layer in `shim/` that satisfies just enough of HemisphereApplet's API surface for Logic.h to compile and run. Per-applet plug-in source is a 3-line wrapper using `NT_HEM_PLUGIN(Logic, ...)`. The shim provides global `frame` / `cvmap[]` / `trigmap[]` / `clock_m` storage, a `graphics` object that writes into NT_screen, a `HemisphereApplet` base class, and a templated `HemisphereShim<T>` that wires the applet into the NT plug-in factory. ARM-only build; host-side tests validate the shim's bus/gate translation logic in isolation, the on-hardware behavior is the integration test.

**Tech Stack:** C++14 (host tests, Catch2 v3), C++11 (ARM target, arm-none-eabi-c++), GNU Make. New files only — no modifications to existing harness/ or vendor/. Logic.h is referenced via include path, not copied.

**Plan decomposition note:** This is Plan B of three. Plan B's scope is intentionally the smallest end-to-end shim + applet pair (Logic.h, the simplest Tier 1 applet — only gates, no CV). Plan C adds the remaining four Tier 1 applets (AttenuateOffset, Slew, Calculate, Burst) which exercise additional surface (CV in/out, mix logic, slew engines, burst gate sequencing). Plan D adds Tier 2 (Brancher, TLNeuron, GateDelay).

**Scope cuts inherited from Plan A rev-5:**

- Screen output is eyeball-validated on hardware, not byte-faithful. Simulator's drawing primitives use the placeholder 6×8 font from `harness/src/font_placeholder.cpp`. If a Hem applet's draws look wrong on hardware, debug ad hoc.
- Tier 1 applets do not use the quantizer, scales, or MIDI surface (re-verified during planning); shim stubs for those return zero/no-op.

---

## File structure

```text
shim/
  include/
    Arduino.h              # constrain, abs, min, max forwards; nothing else
    OC_core.h              # OC::CORE::ticks counter
    OC_DAC.h               # DAC_CHANNEL enum (CH_A..CH_D)
    OC_ADC.h               # ADC_CHANNEL_LAST
    OC_strings.h           # OC::Strings::capital_letters[]
    OC_gpio.h              # NorthernLightModular guard (defines to 0)
    util/util_math.h       # CONSTRAIN macro
    HSicons.h              # ZAP_ICON, CV_ICON, DOWN_BTN_ICON byte arrays
    PhzIcons.h             # PhzIcons::logic byte array
    HSUtils.h              # HEMISPHERE_* constants, Pack/Unpack, HS::HEM_SIDE
    HSIOFrame.h            # HS::IOFrame, CVInputMap fwd decl, frame extern
    HSClockManager.h       # HSClockManager stub returning IsRunning()=false
    CVInputMap.h           # CVInputMap class with In() returning frame value
    HemisphereApplet.h     # base class — subset for Logic
    hem_graphics.h         # OC::graphics-shaped global, declarations
    hem_shim.h             # NT_HEM_PLUGIN macro + HemisphereShim<T> template
  src/
    globals.cpp            # frame, cvmap[], trigmap[], q_engine[] storage
    icons.cpp              # icon byte arrays (only those Logic uses)
    strings.cpp            # capital_letters
    graphics.cpp           # graphics impl (drawFrame, drawLine, drawBitmap8, print, ...)
    hem_shim.cpp           # non-template helpers for HemisphereShim
applets/
  Logic.cpp                # 3-line wrapper: #include hem_shim.h, applet, NT_HEM_PLUGIN
build/arm/Logic.o          # output
```

Each header has one clear responsibility. `HemisphereApplet.h` is the largest file (Logic uses ~30 methods); kept as one header to mirror upstream.

---

## Task 0: Makefile rule for shim build + Logic.o

**Files:**
- Modify: `Makefile`

- [x] **Step 1: Confirm `make arm` currently produces 3 .o files**

Run: `make clean && make arm`
Expected: `build/arm/{gainCustomUI,gain,bus_probe}.o`. No errors.

- [x] **Step 2: Add Logic.o build rule**

Add to `Makefile` after the existing `build/arm/bus_probe.o` rule:

```makefile
# Hem shim sources (header-only for now; compiled as part of each applet's TU)
SHIM_INCLUDE := -Ishim/include
HEM_APPLET_INCLUDE := -Ivendor/O_C-Phazerville/software/src/applets

build/arm/Logic.o: applets/Logic.cpp $(wildcard shim/include/*.h) $(wildcard shim/include/*/*.h) $(wildcard shim/src/*.cpp)
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<
```

Update the `arm` aggregate target to include `Logic.o`:

```makefile
arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/Logic.o
```

- [x] **Step 3: Stub Logic.cpp so the rule has something to compile**

`applets/Logic.cpp`:

```cpp
// Placeholder; real wrapper added in Task 11.
#include <distingnt/api.h>
extern "C" uintptr_t pluginEntry(_NT_selector, uint32_t) { return 0; }
```

- [x] **Step 4: Verify `make arm` succeeds**

Run: `make arm`
Expected: 4 .o files, no errors.

- [x] **Step 5: Commit**

```bash
git add Makefile applets/Logic.cpp
git commit -m "build: Logic.o target with shim include path"
```

---

## Task 1: Minimal Arduino.h + OC_gpio.h + util/util_math.h

**Files:**
- Create: `shim/include/Arduino.h`
- Create: `shim/include/OC_gpio.h`
- Create: `shim/include/util/util_math.h`

- [x] **Step 1: Write Arduino.h**

```cpp
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

template <typename T>
inline T constrain(T x, T lo, T hi) {
    return std::min(std::max(x, lo), hi);
}

#ifndef min
#define min(a, b) std::min((a), (b))
#endif
#ifndef max
#define max(a, b) std::max((a), (b))
#endif
```

- [x] **Step 2: Write OC_gpio.h**

```cpp
#pragma once
// Phazerville T4.1 detection. NorthernLightModular variant uses bigger CV range;
// the NT shim is always the T4.1 layout, so this stays 0.
#define NorthernLightModular 0
```

- [x] **Step 3: Write util/util_math.h**

```cpp
#pragma once
#include <algorithm>

#ifndef CONSTRAIN
#define CONSTRAIN(x, lo, hi) \
    do { if ((x) < (lo)) (x) = (lo); else if ((x) > (hi)) (x) = (hi); } while (0)
#endif
```

- [x] **Step 4: Build sanity**

Run: `make arm`
Expected: still succeeds (Logic.cpp is still a stub).

- [x] **Step 5: Commit**

```bash
git add shim/include/Arduino.h shim/include/OC_gpio.h shim/include/util/util_math.h
git commit -m "shim: Arduino.h, OC_gpio.h, util/util_math.h compat headers"
```

---

## Task 2: OC_core.h + OC_DAC.h + OC_ADC.h + HSUtils.h

**Files:**
- Create: `shim/include/OC_core.h`
- Create: `shim/include/OC_DAC.h`
- Create: `shim/include/OC_ADC.h`
- Create: `shim/include/HSUtils.h`

- [x] **Step 1: OC_core.h**

```cpp
#pragma once
#include <cstdint>

namespace OC {
namespace CORE {
extern volatile uint32_t ticks;  // populated by HemisphereShim each Controller tick
}
}
```

- [x] **Step 2: OC_DAC.h**

```cpp
#pragma once

enum DAC_CHANNEL {
    DAC_CHANNEL_A,
    DAC_CHANNEL_B,
    DAC_CHANNEL_C,
    DAC_CHANNEL_D,
    DAC_CHANNEL_LAST
};
```

- [x] **Step 3: OC_ADC.h**

```cpp
#pragma once

enum { ADC_CHANNEL_LAST = 4 };
```

- [x] **Step 4: HSUtils.h**

This mirrors `vendor/O_C-Phazerville/software/src/HSUtils.h` but trimmed to what Logic actually needs. Verbatim constant definitions so applet's macros expand identically.

```cpp
#pragma once
#include "OC_gpio.h"

#define ONE_OCTAVE (12 << 7)                                    // 1536 hem units per V
#define HEMISPHERE_MAX_INPUT_CV (6 * ONE_OCTAVE)                // 9216 (T4.1)
#define HEMISPHERE_MAX_CV (6 * ONE_OCTAVE)                      // PULSE_VOLTAGE * ONE_OCTAVE
#define HEMISPHERE_CENTER_INPUT_CV 0                            // (NorthernLightModular = 0)
#define HEMISPHERE_CENTER_DETENT 80
#define HEMISPHERE_CLOCK_TICKS 17
#define HEMISPHERE_CURSOR_TICKS 5000

#define PULSE_VOLTAGE 6                                         // octave_max on T4.1

#define ForEachChannel(ch) for (int_fast8_t ch = 0; (ch) < 2; ++(ch))
#define gfx_offset 0                                            // shim renders single applet at left
#define io_offset 0                                             // shim's frame indexes from 0

namespace HS {
enum HEM_SIDE : uint8_t { LEFT_HEMISPHERE = 0, APPLET_CURSOR_COUNT };
enum HELP_SECTIONS {
    HELP_DIGITAL1 = 0, HELP_DIGITAL2,
    HELP_CV1, HELP_CV2,
    HELP_OUT1, HELP_OUT2,
    HELP_EXTRA1, HELP_EXTRA2,
    HELP_LABEL_COUNT
};
}
using namespace HS;

constexpr void Pack(uint64_t& data, struct PackLocation p, uint64_t value);
constexpr int Unpack(const uint64_t& data, struct PackLocation p);

struct PackLocation { size_t location; size_t size; };
constexpr void Pack(uint64_t& data, PackLocation p, uint64_t value) {
    data |= (value << p.location);
}
constexpr int Unpack(const uint64_t& data, PackLocation p) {
    uint64_t mask = 1;
    for (size_t i = 1; i < p.size; ++i) mask |= (uint64_t(1) << i);
    return (data >> p.location) & mask;
}

// Help array — populated by applet's SetHelp, read by debug/help screens.
extern const char* help_strings[HS::HELP_LABEL_COUNT];

// Cursor blink countdown — used by HemisphereApplet::CursorBlink().
extern int cursor_countdown[HS::APPLET_CURSOR_COUNT];

// EditMode toggle state per side. Logic uses just LEFT_HEMISPHERE.
struct EncoderEditor { bool isEditing; };
extern EncoderEditor enc_edit[HS::APPLET_CURSOR_COUNT];
```

- [x] **Step 5: Build sanity + commit**

Run: `make arm`
Expected: still passes (Logic.cpp stub).

```bash
git add shim/include/OC_core.h shim/include/OC_DAC.h shim/include/OC_ADC.h shim/include/HSUtils.h
git commit -m "shim: OC_core.h, OC_DAC.h, OC_ADC.h, HSUtils.h with HEM constants"
```

---

## Task 3: shim/src/globals.cpp + remaining stub headers

**Files:**
- Create: `shim/src/globals.cpp`
- Create: `shim/include/OC_strings.h`
- Create: `shim/include/HSClockManager.h`
- Create: `shim/include/HSicons.h`
- Create: `shim/include/PhzIcons.h`

- [x] **Step 1: globals.cpp — single TU defining the externs**

```cpp
#include "HSUtils.h"
#include "OC_core.h"

namespace OC {
namespace CORE {
volatile uint32_t ticks = 0;
}
}

namespace HS {
const char* help_strings[HS::HELP_LABEL_COUNT] = { nullptr };
int cursor_countdown[HS::APPLET_CURSOR_COUNT] = { 0 };
EncoderEditor enc_edit[HS::APPLET_CURSOR_COUNT] = {{ false }};
}
```

- [x] **Step 2: OC_strings.h**

```cpp
#pragma once

namespace OC {
namespace Strings {
extern const char* const capital_letters[];  // "A","B","C","D",...
}
}
```

Add to `shim/src/globals.cpp`:

```cpp
#include "OC_strings.h"
namespace OC {
namespace Strings {
const char* const capital_letters[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
}
}
```

- [x] **Step 3: HSClockManager.h**

```cpp
#pragma once
#include <cstdint>

class HSClockManager {
public:
    bool IsRunning() const { return false; }
    int  GetMultiply(int) const { return 0; }
    uint32_t GetCycleTicks(int) const { return 0; }
};

extern HSClockManager clock_m;
```

Add to `globals.cpp`:

```cpp
#include "HSClockManager.h"
HSClockManager clock_m;
```

- [x] **Step 4: HSicons.h + PhzIcons.h**

`shim/include/HSicons.h`:

```cpp
#pragma once
#include <cstdint>
extern const uint8_t ZAP_ICON[8];
extern const uint8_t CV_ICON[8];
extern const uint8_t DOWN_BTN_ICON[8];
```

`shim/include/PhzIcons.h`:

```cpp
#pragma once
#include <cstdint>
namespace PhzIcons {
extern const uint8_t logic[8];
}
```

`shim/src/icons.cpp`:

```cpp
#include "HSicons.h"
#include "PhzIcons.h"

// Byte arrays mirror upstream Phazerville icon definitions.
// 8x8 bitmaps, one byte per column, low bit = top row.

const uint8_t ZAP_ICON[8]      = { 0x00, 0x40, 0x60, 0x7e, 0x7e, 0x06, 0x02, 0x00 };
const uint8_t CV_ICON[8]       = { 0x66, 0x7e, 0x3c, 0x18, 0x18, 0x3c, 0x7e, 0x66 };
const uint8_t DOWN_BTN_ICON[8] = { 0x00, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18 };

namespace PhzIcons {
const uint8_t logic[8] = { 0x3c, 0x42, 0x99, 0xa5, 0xa5, 0x99, 0x42, 0x3c };  // placeholder; replace with upstream bytes
}
```

Engineer note: the upstream `PhzIcons::logic` exact bytes live in `vendor/O_C-Phazerville/software/src/PhzIcons.h`. Copy verbatim, replacing the placeholder. Same for any HS icon Logic uses if the bytes here turn out wrong on hardware.

- [x] **Step 5: Add globals.cpp + icons.cpp to Makefile**

Update the `Logic.o` rule to include shim cpp:

```makefile
build/arm/Logic.o: applets/Logic.cpp \
                   $(wildcard shim/include/*.h) $(wildcard shim/include/*/*.h) \
                   shim/src/globals.cpp shim/src/icons.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<
```

Note: `Logic.cpp` will `#include` the shim .cpp files indirectly via headers. To avoid multiple-definition errors, shim source must be header-included into Logic.cpp's translation unit, not separately compiled. Easiest: make `globals.cpp` and `icons.cpp` actually be `.inl` files included once from `Logic.cpp` via `hem_shim.h`. We do that in Task 9.

For now, leave the shim .cpp files on disk but don't add them to the compile line. Each applet .cpp will pull them in via the shim header.

- [x] **Step 6: Build + commit**

Run: `make arm`
Expected: still passes.

```bash
git add shim/
git commit -m "shim: globals storage + clock manager stub + icons + strings"
```

---

## Task 4: HSIOFrame.h + CVInputMap.h

**Files:**
- Create: `shim/include/HSIOFrame.h`
- Create: `shim/include/CVInputMap.h`

- [ ] **Step 1: HSIOFrame.h**

The minimal `IOFrame` covers In/Out/Gate/Clock + cycle_ticks + changed_cv + adc_lag_countdown (all that HemisphereApplet reads). For Logic, we only need `inputs[]`, `outputs[]`, `clocked[]` indexed by `io_offset + ch`.

```cpp
#pragma once
#include <cstdint>
#include "OC_DAC.h"

namespace HS {

struct OutputCell {
    int value = 0;
    int target = 0;
    int get_target() const { return target; }
    void set(int v) { value = target = v; }
};

struct IOFrame {
    int  inputs[4]            = { 0 };  // 4 inputs (T4.1)
    OutputCell outputs[4];               // 4 outputs (T4.1)
    bool clocked[4]           = { false };
    bool changed_cv[4]        = { false };
    uint32_t cycle_ticks[4]   = { 0 };
    int  adc_lag_countdown[4] = { -1 };
    uint32_t tick             = 0;

    void Out(DAC_CHANNEL ch, int value) { outputs[ch].set(value); }
    int  ViewOut(int ch) const { return outputs[ch].value; }
    void ClockOut(DAC_CHANNEL ch, int ticks);  // defined in globals.cpp
};

extern IOFrame frame;

}  // namespace HS

using namespace HS;
```

Append to `shim/src/globals.cpp`:

```cpp
#include "HSIOFrame.h"
HS::IOFrame HS::frame;

void HS::IOFrame::ClockOut(DAC_CHANNEL ch, int) {
    // Simplest: drive the output high for one Controller tick by setting value high.
    // The host's bus write picks up the value next time.
    outputs[ch].set(PULSE_VOLTAGE * ONE_OCTAVE);
}
```

- [ ] **Step 2: CVInputMap.h**

Logic calls `cvmap[ch].In()`. Minimal CVInputMap reads from the global `frame.inputs[ch]`.

```cpp
#pragma once
#include "HSIOFrame.h"

class CVInputMap {
public:
    int channel = 0;
    int In() const { return HS::frame.inputs[channel]; }
};

extern CVInputMap cvmap[4];
```

Append to `globals.cpp`:

```cpp
#include "CVInputMap.h"
CVInputMap cvmap[4] = {{0}, {1}, {2}, {3}};
```

Logic also calls `trigmap[ch + io_offset].Gate()`. Provide `DigitalInputMap`:

```cpp
#pragma once
// (add to CVInputMap.h or new file digital_input_map.h)
class DigitalInputMap {
public:
    int channel = 0;
    bool Gate() const { return HS::frame.clocked[channel]; }
};

extern DigitalInputMap trigmap[4];
```

Append to `globals.cpp`:

```cpp
DigitalInputMap trigmap[4] = {{0}, {1}, {2}, {3}};
```

- [ ] **Step 3: Build + commit**

Run: `make arm`
Expected: still passes (Logic.cpp stub).

```bash
git add shim/
git commit -m "shim: HSIOFrame.h + CVInputMap.h + DigitalInputMap"
```

---

## Task 5: hem_graphics.h + graphics.cpp

**Files:**
- Create: `shim/include/hem_graphics.h`
- Create: `shim/src/graphics.cpp`

OC's `graphics` object is an instance of `weegfx::Graphics`. Hemisphere calls these methods: `setPrintPos`, `print(int)`, `print(const char*)`, `printf(...)`, `drawFrame`, `drawRect`, `invertRect`, `clearRect`, `drawLine`, `drawCircle`, `drawBitmap8`, `setPixel`, `getPrintPosX/Y`. Logic uses only a subset: `drawFrame`, `drawBitmap8`, `setPrintPos`, `print(const char*)`. But for Tier 1 we'll need the full set, so build them all now.

- [ ] **Step 1: hem_graphics.h**

```cpp
#pragma once
#include <cstdint>

namespace shim {

class Graphics {
public:
    void setPrintPos(int x, int y) { print_x = x; print_y = y; }
    int  getPrintPosX() const { return print_x; }
    int  getPrintPosY() const { return print_y; }

    void print(const char* s);
    void print(int n);

    void setPixel(int x, int y);
    void drawLine(int x0, int y0, int x1, int y1, uint8_t pattern = 1);
    void drawFrame(int x, int y, int w, int h);
    void drawRect(int x, int y, int w, int h);
    void invertRect(int x, int y, int w, int h);
    void clearRect(int x, int y, int w, int h);
    void drawCircle(int x, int y, int r);
    void drawBitmap8(int x, int y, int w, const uint8_t* data);

private:
    int print_x = 0;
    int print_y = 0;
};

}  // namespace shim

extern shim::Graphics graphics;
```

- [ ] **Step 2: graphics.cpp**

Pixel writes go into `NT_screen` (256×64, 4-bit packed). Use the same set_pixel pattern as harness/src/nt_runtime.cpp:

```cpp
#include "hem_graphics.h"
#include <distingnt/api.h>
#include <cstring>
#include <cstdlib>

shim::Graphics graphics;

namespace {

inline void set_pixel(int x, int y, int colour) {
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
    int byte_index  = y * 128 + (x >> 1);
    uint8_t mask    = (x & 1) ? 0xf0 : 0x0f;
    uint8_t shifted = (uint8_t)((colour & 0x0f) << ((x & 1) ? 4 : 0));
    NT_screen[byte_index] = (uint8_t)((NT_screen[byte_index] & ~mask) | shifted);
}

// Minimal 6x8 placeholder font: every printable glyph is a solid 6x8 block.
// Replace with the OC weegfx font when visual accuracy is needed (Plan C).
const uint8_t* glyph_for(char c) {
    static const uint8_t solid[6] = { 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e };
    static const uint8_t blank[6] = { 0, 0, 0, 0, 0, 0 };
    return (c < 32 || c > 126) ? blank : solid;
}

}  // anonymous namespace

namespace shim {

void Graphics::print(const char* s) {
    if (!s) return;
    int cx = print_x;
    for (; *s; ++s) {
        const uint8_t* g = glyph_for(*s);
        for (int col = 0; col < 6; ++col) {
            uint8_t bits = g[col];
            for (int row = 0; row < 8; ++row) {
                if (bits & (1u << row)) set_pixel(cx + col, print_y + row, 15);
            }
        }
        cx += 6;
    }
    print_x = cx;
}

void Graphics::print(int n) {
    char buf[12];
    int written = NT_intToString(buf, n);
    buf[written] = 0;
    print(buf);
}

void Graphics::setPixel(int x, int y) { set_pixel(x, y, 15); }

void Graphics::drawLine(int x0, int y0, int x1, int y1, uint8_t /*pattern*/) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        set_pixel(x0, y0, 15);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
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
            uint8_t mask = (xx & 1) ? 0xf0 : 0x0f;
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
```

- [ ] **Step 3: Build + commit**

Run: `make arm`
Expected: still passes (graphics.cpp not yet linked into Logic.o; we'll wire that in Task 9).

```bash
git add shim/
git commit -m "shim: graphics object writing into NT_screen (placeholder font)"
```

---

## Task 6: HemisphereApplet.h — base class

**Files:**
- Create: `shim/include/HemisphereApplet.h`

This is the largest single file. Mirrors `vendor/O_C-Phazerville/software/src/HemisphereApplet.h` but trimmed to only the methods Logic uses, plus all method declarations of the upstream class (so the applet compiles even if some method is referenced indirectly via virtual dispatch).

- [ ] **Step 1: Write HemisphereApplet.h**

```cpp
#pragma once
#include <cstdint>
#include "Arduino.h"
#include "OC_core.h"
#include "OC_DAC.h"
#include "OC_strings.h"
#include "util/util_math.h"
#include "HSicons.h"
#include "PhzIcons.h"
#include "HSClockManager.h"
#include "HSUtils.h"
#include "HSIOFrame.h"
#include "CVInputMap.h"
#include "hem_graphics.h"

class HemisphereApplet {
public:
    static int cursor_countdown_arr[HS::APPLET_CURSOR_COUNT];

    virtual const char* applet_name() = 0;
    virtual const uint8_t* applet_icon() { return ZAP_ICON; }

    virtual void Start() = 0;
    virtual void Reset() {}
    virtual void Controller() = 0;
    virtual void View() = 0;
    virtual uint64_t OnDataRequest() = 0;
    virtual void OnDataReceive(uint64_t data) = 0;
    virtual void OnButtonPress() { CursorToggle(); }
    virtual void OnEncoderMove(int direction) = 0;
    virtual void AuxButton() {}

    void BaseStart(HS::HEM_SIDE side) { hemisphere = side; Start(); }

    bool CursorBlink() const { return (HS::cursor_countdown[hemisphere] > 0); }
    void ResetCursor() { HS::cursor_countdown[hemisphere] = HEMISPHERE_CURSOR_TICKS; }
    void CursorToggle() { HS::enc_edit[hemisphere].isEditing ^= 1; ResetCursor(); }
    inline bool EditMode() const { return HS::enc_edit[hemisphere].isEditing; }

    template <typename T>
    void MoveCursor(T& cursor, int direction, int max) {
        cursor += direction;
        if (cursor < 0) cursor = 0;
        if (cursor > max) cursor = max;
        ResetCursor();
    }

    // I/O
    int  In(int ch)        { return cvmap[ch].In(); }
    int  ViewIn(int ch) const { return HS::frame.inputs[ch]; }
    int  ViewOut(int ch) const { return HS::frame.ViewOut(ch); }
    bool Clock(int ch, bool = false) { return HS::frame.clocked[ch]; }
    bool Gate(int ch)      { return trigmap[ch].Gate(); }
    int  DetentedIn(int ch) {
        int v = In(ch);
        if (v > HEMISPHERE_CENTER_DETENT) return v;
        if (v < -HEMISPHERE_CENTER_DETENT) return v;
        return 0;
    }

    void Out(int ch, int value)         { HS::frame.Out((DAC_CHANNEL)(ch), value); }
    void ClockOut(int ch, int ticks = HEMISPHERE_CLOCK_TICKS) { HS::frame.ClockOut((DAC_CHANNEL)(ch), ticks); }
    void GateOut(int ch, bool high)     { Out(ch, high ? (PULSE_VOLTAGE * ONE_OCTAVE) : 0); }

    int  ProportionCV(int cv, int max_pixels, int max_cv = HEMISPHERE_MAX_CV) const {
        long prop = (long)cv * max_pixels / (max_cv > 0 ? max_cv : 1);
        return constrain((int)prop, 0, max_pixels);
    }

    // gfx wrappers — all draw to the shim's graphics global, with offsets honored.
    void gfxPos(int x, int y)                  { graphics.setPrintPos(x + gfx_offset, y); }
    void gfxPrint(const char* s)               { graphics.print(s); }
    void gfxPrint(int n)                       { graphics.print(n); }
    void gfxPrint(int x, int y, const char* s) { gfxPos(x, y); gfxPrint(s); }
    void gfxPrint(int x, int y, int n)         { gfxPos(x, y); gfxPrint(n); }

    void gfxFrame(int x, int y, int w, int h)  { graphics.drawFrame(x + gfx_offset, y, w, h); }
    void gfxRect(int x, int y, int w, int h)   { graphics.drawRect(x + gfx_offset, y, w, h); }
    void gfxInvert(int x, int y, int w, int h) { graphics.invertRect(x + gfx_offset, y, w, h); }
    void gfxClear(int x, int y, int w, int h)  { graphics.clearRect(x + gfx_offset, y, w, h); }
    void gfxLine(int x, int y, int x2, int y2) { graphics.drawLine(x + gfx_offset, y, x2 + gfx_offset, y2); }
    void gfxPixel(int x, int y)                { graphics.setPixel(x + gfx_offset, y); }
    void gfxCircle(int x, int y, int r)        { graphics.drawCircle(x + gfx_offset, y, r); }

    void gfxBitmap(int x, int y, int w, const uint8_t* data) {
        graphics.drawBitmap8(x + gfx_offset, y, w, data);
    }
    void gfxIcon(int x, int y, const uint8_t* data, bool /*clearfirst*/ = false) {
        gfxBitmap(x, y, 8, data);
    }

    void gfxCursor(int x, int y, int w, int h = 9) {
        if (EditMode()) {
            gfxInvert(x, y - h, w, h);
        } else if (CursorBlink()) {
            gfxLine(x, y, x + w - 1, y);
        }
    }

    const char* OutputLabel(int ch) const { return OC::Strings::capital_letters[ch]; }

protected:
    virtual void SetHelp() = 0;
    HS::HEM_SIDE hemisphere = HS::LEFT_HEMISPHERE;
};

// Convenience alias for applets that reference the global help[] array directly.
#define help (HS::help_strings)
```

- [ ] **Step 2: Build sanity + commit**

Run: `make arm`
Expected: still passes (Logic.cpp stub doesn't include the base class yet).

```bash
git add shim/include/HemisphereApplet.h
git commit -m "shim: HemisphereApplet base class for Logic"
```

---

## Task 7: hem_shim.h — NT_HEM_PLUGIN macro + HemisphereShim<T>

**Files:**
- Create: `shim/include/hem_shim.h`

This is the heart of the shim. It defines:

- `HemisphereShim<T>`: a template that wraps the applet instance with a `_NT_algorithm` subclass.
- `NT_HEM_PLUGIN(klass, guid, name, desc)`: a macro that expands to the NT factory boilerplate.

For Logic (gate-only, no CV out), the param layout is: Gate In 1 (bus), Gate In 2 (bus), CV In 1 (bus, optional), CV In 2 (bus, optional), Gate Out 1 (bus + mode), Gate Out 2 (bus + mode). Six bus params total.

- [ ] **Step 1: Write hem_shim.h**

```cpp
#pragma once
#include <distingnt/api.h>
#include <new>
#include <cstring>
#include "HemisphereApplet.h"
#include "HSIOFrame.h"

namespace hem_shim {

// Param indices in the order they appear in parameters[].
enum {
    kParamGateIn1, kParamGateIn2,
    kParamCvIn1,   kParamCvIn2,
    kParamCvOut1,  kParamCvOut1Mode,
    kParamCvOut2,  kParamCvOut2Mode,
    kParamCount
};

// Shared parameter table for every shim plug-in (no applet-specific params yet).
inline const _NT_parameter* shim_parameters() {
    static const _NT_parameter params[] = {
        NT_PARAMETER_CV_INPUT("Gate In 1", 0, 3)
        NT_PARAMETER_CV_INPUT("Gate In 2", 0, 4)
        NT_PARAMETER_CV_INPUT("CV In 1",   0, 1)
        NT_PARAMETER_CV_INPUT("CV In 2",   0, 2)
        NT_PARAMETER_CV_OUTPUT_WITH_MODE("CV Out 1", 0, 15)
        NT_PARAMETER_CV_OUTPUT_WITH_MODE("CV Out 2", 0, 16)
    };
    return params;
}

inline const _NT_parameterPages* shim_parameter_pages() {
    static const uint8_t routing_page[] = {
        kParamGateIn1, kParamGateIn2, kParamCvIn1, kParamCvIn2,
        kParamCvOut1, kParamCvOut1Mode, kParamCvOut2, kParamCvOut2Mode
    };
    static const _NT_parameterPage pages[] = {
        { .name = "Routing", .numParams = sizeof(routing_page), .params = routing_page },
    };
    static const _NT_parameterPages parameterPages = {
        .numPages = 1, .pages = pages,
    };
    return &parameterPages;
}

// Read NT bus -> HS frame at start of each step block.
template <typename T>
struct AlgorithmInstance : public _NT_algorithm {
    T applet;
    bool started = false;
};

template <typename T>
struct Shim {
    static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
        req.numParameters = kParamCount;
        req.sram = sizeof(AlgorithmInstance<T>);
    }

    static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                    const _NT_algorithmRequirements&, const int32_t*) {
        auto* alg = new (ptrs.sram) AlgorithmInstance<T>();
        alg->parameters     = shim_parameters();
        alg->parameterPages = shim_parameter_pages();
        return alg;
    }

    static void copy_bus_to_frame(int bus_param, int* dst, float* busFrames, int numFrames,
                                  const int16_t* v) {
        int bus = v[bus_param];
        if (bus <= 0) { *dst = 0; return; }
        // Average the block as the input value for this Controller tick.
        const float* src = busFrames + (bus - 1) * numFrames;
        float sum = 0.0f;
        for (int i = 0; i < numFrames; ++i) sum += src[i];
        float mean = sum / (float)numFrames;
        *dst = (int)(mean * 1536.0f);  // 1.0f = 1V = ONE_OCTAVE
    }

    static bool read_gate(int bus_param, float* busFrames, int numFrames, const int16_t* v,
                          bool& prev_high) {
        int bus = v[bus_param];
        if (bus <= 0) { prev_high = false; return false; }
        const float* src = busFrames + (bus - 1) * numFrames;
        bool any_high = false;
        bool rising = false;
        for (int i = 0; i < numFrames; ++i) {
            bool high = (src[i] > 1.0f);
            if (high && !prev_high) rising = true;
            prev_high = high;
            any_high = any_high || high;
        }
        return rising;  // returns true once per block on any rising edge
    }

    static void write_frame_to_bus(int bus_param, int mode_param, int value_hem,
                                   float* busFrames, int numFrames, const int16_t* v) {
        int bus = v[bus_param];
        if (bus <= 0) return;
        float* dst = busFrames + (bus - 1) * numFrames;
        float value_nt = (float)value_hem / 1536.0f;
        bool replace = v[mode_param];
        if (replace) {
            for (int i = 0; i < numFrames; ++i) dst[i] = value_nt;
        } else {
            for (int i = 0; i < numFrames; ++i) dst[i] += value_nt;
        }
    }

    // Per-instance gate state for rising-edge detection across blocks.
    static bool& prev_gate(int idx) {
        static bool prev[2] = { false, false };
        return prev[idx];
    }

    static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        int numFrames = numFramesBy4 * 4;
        const int16_t* v = alg->v;

        // Populate HS::frame from NT buses
        copy_bus_to_frame(kParamCvIn1, &HS::frame.inputs[0], busFrames, numFrames, v);
        copy_bus_to_frame(kParamCvIn2, &HS::frame.inputs[1], busFrames, numFrames, v);
        HS::frame.clocked[0] = read_gate(kParamGateIn1, busFrames, numFrames, v, prev_gate(0));
        HS::frame.clocked[1] = read_gate(kParamGateIn2, busFrames, numFrames, v, prev_gate(1));

        if (!alg->started) {
            alg->applet.BaseStart(HS::LEFT_HEMISPHERE);
            alg->started = true;
        }

        // Run a single Controller tick per block (cheap; expand to N ticks per block in Plan C if needed)
        OC::CORE::ticks += 1;
        alg->applet.Controller();

        // Push frame outputs back to NT buses
        write_frame_to_bus(kParamCvOut1, kParamCvOut1Mode, HS::frame.outputs[0].value,
                          busFrames, numFrames, v);
        write_frame_to_bus(kParamCvOut2, kParamCvOut2Mode, HS::frame.outputs[1].value,
                          busFrames, numFrames, v);
    }

    static bool draw(_NT_algorithm* self) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        if (!alg->started) return false;
        alg->applet.View();
        return false;  // do not suppress NT's parameter line
    }

    static uint32_t hasCustomUi(_NT_algorithm*) {
        return kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR;
    }

    static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        if (data.encoders[0] != 0) alg->applet.OnEncoderMove(data.encoders[0]);
        if (data.encoders[1] != 0) {
            // R encoder synthesises EditMode true for the duration of the call
            bool was = HS::enc_edit[HS::LEFT_HEMISPHERE].isEditing;
            HS::enc_edit[HS::LEFT_HEMISPHERE].isEditing = true;
            alg->applet.OnEncoderMove(data.encoders[1]);
            HS::enc_edit[HS::LEFT_HEMISPHERE].isEditing = was;
        }
        if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
            alg->applet.OnButtonPress();
        }
        if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR)) {
            alg->applet.AuxButton();
        }
    }

    static void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        uint64_t state = alg->applet.OnDataRequest();
        uint32_t hi = (uint32_t)(state >> 32);
        uint32_t lo = (uint32_t)(state & 0xFFFFFFFFu);
        stream.addMemberName("hem_hi"); stream.addNumber((int)hi);
        stream.addMemberName("hem_lo"); stream.addNumber((int)lo);
    }

    static bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        int num_members = 0;
        if (!parse.numberOfObjectMembers(num_members)) return false;
        int hi = 0, lo = 0;
        for (int i = 0; i < num_members; ++i) {
            if      (parse.matchName("hem_hi")) { if (!parse.number(hi)) return false; }
            else if (parse.matchName("hem_lo")) { if (!parse.number(lo)) return false; }
            else                                { if (!parse.skipMember()) return false; }
        }
        uint64_t state = ((uint64_t)(uint32_t)hi << 32) | (uint64_t)(uint32_t)lo;
        alg->applet.OnDataReceive(state);
        return true;
    }
};

}  // namespace hem_shim

#define NT_HEM_PLUGIN(klass, guid_str_4chars, name_str, desc_str) \
    static const _NT_factory _hem_factory = { \
        .guid = NT_MULTICHAR(guid_str_4chars[0], guid_str_4chars[1], \
                             guid_str_4chars[2], guid_str_4chars[3]), \
        .name = name_str, \
        .description = desc_str, \
        .numSpecifications = 0, \
        .calculateRequirements = hem_shim::Shim<klass>::calculateRequirements, \
        .construct             = hem_shim::Shim<klass>::construct, \
        .step                  = hem_shim::Shim<klass>::step, \
        .draw                  = hem_shim::Shim<klass>::draw, \
        .tags                  = kNT_tagUtility, \
        .hasCustomUi           = hem_shim::Shim<klass>::hasCustomUi, \
        .customUi              = hem_shim::Shim<klass>::customUi, \
        .serialise             = hem_shim::Shim<klass>::serialise, \
        .deserialise           = hem_shim::Shim<klass>::deserialise, \
    }; \
    extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) { \
        switch (selector) { \
        case kNT_selector_version:      return kNT_apiVersionCurrent; \
        case kNT_selector_numFactories: return 1; \
        case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &_hem_factory : nullptr); \
        } \
        return 0; \
    }
```

- [ ] **Step 2: Build sanity + commit**

Run: `make arm`
Expected: still passes.

```bash
git add shim/include/hem_shim.h
git commit -m "shim: hem_shim.h with NT_HEM_PLUGIN macro and HemisphereShim<T>"
```

---

## Task 8: Wire shim cpp files into the Logic.o translation unit

**Files:**
- Create: `shim/include/hem_shim_impl.h` (single-include of all shim cpp content)

To avoid multiple-definition errors with multiple applets in a future build, shim source is `#include`d into each applet's TU rather than separately compiled.

- [ ] **Step 1: hem_shim_impl.h**

```cpp
#pragma once
// One-stop include that brings in all shim implementations.
// Each applet .cpp must include this exactly once.
#include "../src/globals.cpp"
#include "../src/icons.cpp"
#include "../src/graphics.cpp"
```

- [ ] **Step 2: Update hem_shim.h to include hem_shim_impl.h once per TU**

Add at the top of `hem_shim.h`:

```cpp
#ifndef NT_HEM_NO_IMPL
#include "hem_shim_impl.h"
#define NT_HEM_NO_IMPL 1
#endif
```

- [ ] **Step 3: Verify build still succeeds**

Run: `make arm`
Expected: passes (Logic.cpp still stub, doesn't include hem_shim.h yet).

- [ ] **Step 4: Commit**

```bash
git add shim/
git commit -m "shim: hem_shim_impl.h aggregates shim cpp sources for single-TU build"
```

---

## Task 9: Replace Logic.cpp stub with the real 3-line wrapper

**Files:**
- Modify: `applets/Logic.cpp`

- [ ] **Step 1: Write the wrapper**

```cpp
#include "hem_shim.h"
#include "Logic.h"

NT_HEM_PLUGIN(Logic, "Hl01", "Hem: Logic", "Phazerville Hemisphere Logic applet")
```

- [ ] **Step 2: Build Logic.o**

Run: `make arm`
Expected: clean build with no warnings under `-Wall`. Logic.o size in the same ballpark as gainCustomUI.o (5-10 KB).

If errors occur: most likely missing constant, method, or class member. Add the minimum to the relevant shim header. Common gaps and where to add them:

- "no member named X in HemisphereApplet" → add to `shim/include/HemisphereApplet.h`
- "ZAP_ICON not declared" → confirm `shim/include/HSicons.h` declares it AND `shim/src/icons.cpp` defines it
- "frame not declared" → confirm `using namespace HS` is in scope
- "constrain not declared" → confirm `Arduino.h` was included

- [ ] **Step 3: Confirm Logic.o has the expected NT plug-in symbols**

```bash
arm-none-eabi-nm build/arm/Logic.o | grep -E "pluginEntry|_hem_factory" | head
```

Expected: pluginEntry as a global symbol, _hem_factory as a local.

- [ ] **Step 4: Commit**

```bash
git add applets/Logic.cpp
git commit -m "applets: Logic.cpp wrapper for Phazerville Logic applet"
```

---

## Task 10: Deploy + verify Logic loads on hardware

**Depends on:** Task 18a from Plan A (deploy mechanism), NT module connected.

**Files:**
- Create: `tests/reference/applets/Logic/load.log` (text capture of what NT's View info screen showed)

- [ ] **Step 1: Deploy via nt_helper (preferred) or USB disk mode**

If nt_helper is running: Plugin Manager → upload `build/arm/Logic.o`.

If not: NT → Misc → Enter USB disk mode; `make deploy DEVICE='/Volumes/DISTING NT'`; eject; reboot.

After upload + reboot: NT → Misc → Plug-ins → View info...
Look for entry `Logic.o: PASSED itc:... dtc:... dram:...`

- [ ] **Step 2: Record what NT says**

Capture the line from View info into `tests/reference/applets/Logic/load.log`. Example:

```
Logic.o: PASSED itc: 1234 dtc: 456 dram: 88
```

If FAILED, the message will name a reason (memory budget exceeded, API version mismatch, etc.). Debug and re-deploy.

- [ ] **Step 3: Commit**

```bash
git add tests/reference/applets/Logic/load.log
git commit -m "test(reference): Logic.o loads on NT (memory stats recorded)"
```

---

## Task 11: Verify Logic applet behavior on hardware

**Depends on:** Task 10 PASSED.

**Files:**
- Create: `tests/reference/applets/Logic/behavior.log`

Logic is a dual gate combiner. Two gate inputs (A, B). Two outputs (channel 0 = first op, channel 1 = second op). Each channel selects one of: AND, OR, XOR, NAND, NOR, XNOR, or -CV- (CV-controlled op).

- [ ] **Step 1: Build a verification preset on the NT**

Empty preset. Add **Hem: Logic** plug-in to slot 1. Configure:

- Gate In 1: Input 1
- Gate In 2: Input 2
- CV Out 1: Output 1, Mode = Replace
- CV Out 2: Output 2, Mode = Replace

Slot 2: any built-in clock divider or gate source that can drive Inputs 1 and 2 (e.g. **Clock** algorithm with two outputs to Output 3 and Output 4, then patch outputs 3/4 to inputs 1/2 with physical cables).

OR use `bus_probe` in slots 2 + 3 to drive the input buses with steady high (1.5V) or low (0V) test patterns.

- [ ] **Step 2: Test AND on channel 0**

On the Logic algo screen: turn left encoder to highlight first operator, press to enter edit mode, turn right encoder to select "AND".

Set Gate In 1 = high (3.3V via bus_probe or external), Gate In 2 = high.
Measure Output 1: expected ≈ 5-6V (gate high = PULSE_VOLTAGE * ONE_OCTAVE / 1536 = 6V).

Set Gate In 1 = high, Gate In 2 = low.
Measure Output 1: expected 0V.

Set Gate In 1 = low, Gate In 2 = high.
Measure Output 1: expected 0V.

Set Gate In 1 = low, Gate In 2 = low.
Measure Output 1: expected 0V.

If all four match, AND works.

- [ ] **Step 3: Test OR on channel 1**

Set operator on channel 1 to "OR".

Truth table: only (low, low) → 0V; the other three → ~6V.

- [ ] **Step 4: Record observations**

Write `tests/reference/applets/Logic/behavior.log`:

```
Date: <yyyy-mm-dd>
Logic.o, channel 0 = AND, channel 1 = OR

In1  In2   Out1 (AND)  Out2 (OR)   notes
low  low   0.00 V       0.00 V
low  high  0.00 V       ~6 V
high low   0.00 V       ~6 V
high high  ~6 V         ~6 V
```

- [ ] **Step 5: Eyeball View() rendering**

Capture screen: `python3 harness/scripts/nt_screenshot.py --pgm tests/reference/applets/Logic/view.pgm`

Open the PGM in Preview. Expected: text showing the two operator names (e.g. "AND" and "OR") plus the logic-gate bitmap icon. Visual style is "looks like Hemisphere"; exact pixel match is not required (per Plan A rev-5 scope cut).

- [ ] **Step 6: Test encoder UI**

Turn left encoder: cursor moves between op selectors. Click L: enters edit mode (cursor inverts). Turn R: cycles operators. Click R: cancels edit.

Confirm each transition by eye.

- [ ] **Step 7: Test preset save/recall**

NT → Preset → Save → name. Power-cycle module. NT → Preset → Load → same name. Confirm Logic algo reloads with same operators selected.

- [ ] **Step 8: Commit**

```bash
git add tests/reference/applets/Logic/
git commit -m "test(reference): Logic applet behavior verified on hardware (AND, OR truth tables)"
```

---

## Done criteria for Plan B

- `build/arm/Logic.o` builds clean from unmodified `Logic.h` via the shim, in under 10 KB.
- Logic.o deploys to NT and shows PASSED in View info.
- Logic applet runs on hardware: AND and OR truth tables match expected; View() draws operator names and icons; encoder UI navigates and edits; preset save/recall preserves state.
- `tests/reference/applets/Logic/{load,behavior}.log` and `view.pgm` recorded.
- No edits made to `vendor/O_C-Phazerville/software/src/applets/Logic.h`.

When Plan B is done, signal readiness for Plan C (AttenuateOffset, Slew, Calculate, Burst).

---

## Self-review

- **Spec coverage:** R1 (unmodified upstream applet) ✓; R2 (shim provides dependencies) ✓ (for Logic's surface); R3 (bus reads, CV scaling, gate detection) ✓ (Tasks 7-9); R4 (bus mapping via NT_PARAMETER macros) ✓ (Task 7's `shim_parameters()`); R5 (persistence via serialise/deserialise) ✓ (Task 7); R6 (clean ARM build) ✓ (Task 0); R7 (host simulator) → out of scope for Plan B (per Plan A rev-5, screen parity dropped; sim already exists for prior tasks). UI mapping (encoder L/R synthesised EditMode for R encoder) ✓ (Task 7's customUi).
- **Placeholder scan:** PhzIcons::logic in Task 3 is "placeholder; replace with upstream bytes" — engineer note tells them where to look. Not a hidden TODO.
- **Type consistency:** `HS::frame.inputs[]`, `HS::frame.outputs[].value`, `HS::frame.clocked[]` used in Tasks 4 and 7 — consistent. `HemisphereApplet::View()`, `Controller()`, `OnEncoderMove(int)` referenced in Task 6 (declaration) and Task 7 (call) — consistent. NT_HEM_PLUGIN macro defined in Task 7 and used in Task 9 — consistent.
- **Iterations expected:** Tasks 1-7 likely compile without iteration. Task 9 (real Logic.cpp) is where compilation errors land; fix in the relevant shim header. Tasks 10-11 are hardware checks where small bugs surface (wrong icon bytes, wrong gate threshold, missing OutputCell::get_target, etc.). Each is a re-deploy cycle, ~1 second via nt_helper.
