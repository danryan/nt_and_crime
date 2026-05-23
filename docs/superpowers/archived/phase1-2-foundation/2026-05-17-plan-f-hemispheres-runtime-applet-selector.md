# Plan F: Hemispheres Plug-in (Runtime Applet Selector)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace per-applet shim plug-ins (`Hl01`, `HaO1`, `Hs01`, `HCal`, `Hbst`) and the hardcoded pair canary (`HpLC`) with a single `Hemispheres` plug-in (GUID `hemi`) that exposes two runtime applet selectors via parameters. User picks Left and Right applets from a 6-entry enum (Empty + 5 Tier 1 applets). Live swap supported; state resets on swap.

**Architecture:** One `_NT_factory`. Per-instance sram holds two fixed-size applet slots sized to the largest known applet (`std::max` of all sizeofs). A factory table maps enum index to placement-new helpers. `step()` head compares cached selector indices to live param values; on change, destructs old applet, zeros side I/O state, placement-news new applet, calls `BaseStart`, refreshes cached index. `draw()` flips `HS::gfx_offset` 0 → 128 between `View()` calls. Serialise persists selector indices alongside applet state; deserialise reconstructs applets before feeding their state back. Each applet has its own translation unit under `applets/<Applet>.cpp` (the "adapter") for per-applet compile isolation and future NT-specific glue; adapters are partial-linked into the single `Hemispheres.o` plug-in object via `arm-none-eabi-ld -r`.

**Tech Stack:** C++11 (arm-none-eabi-c++ for ARM), existing shim from Plans B/C/D/E, distingNT_API v13.

**Pre-req:** Plan E merged to main (commit `eb7e75c`).

---

## Scope decisions

| Question | Decision | Reason |
|----------|----------|--------|
| Single-applet mode? | No, pair-only | User direction; Empty applet covers "only want one" use case |
| Applet selection mechanism | Runtime parameters (not NT specifications) | NT specs are add-time only; params allow live swap |
| Page layout | Setup + Routing | Clean split between rare-edit and frequent-edit params |
| Empty applet visual | "Pick applet" hint text centered on its half | Self-documenting onboarding |
| Default selectors | Both Empty | Predictable blank slate, zero CPU until user picks |
| Applet state on swap | Reset (no preservation) | User mental model: picked different applet = different state |
| Per-applet adapter files | Kept under `applets/<Applet>.cpp` | Per-applet compile isolation, refactor surface, NT-side glue landing zone |
| Combining adapters into single plug-in `.o` | Partial linking (`ld -r`) | Each adapter compiled normally; final pass merges into `Hemispheres.o` |
| Standalone per-applet `.o` plug-ins | No longer built | Hemispheres is sole consumer of adapters |
| Pre-Plan-F built `.o` artifacts on disk | Kept | Don't delete artifacts; user controls device contents |
| Sram budget audit | Not a concern at applet scale | Each applet's runtime state is dozens to low-hundreds of bytes; `2 * kMaxAppletSize` per slot stays sub-KB even at full enum |

---

## File structure

| File | Action | Responsibility |
|------|--------|----------------|
| `applets/Empty.h` | Create | EmptyApplet class: no-op Controller/state, View hint |
| `shim/include/HemisphereApplet.h` | Modify | Add `virtual ~HemisphereApplet() = default` |
| `shim/include/HemispheresFactory.h` | Create | Enum, name strings, factory function table, sram size/align constexpr |
| `shim/include/hem_shim.h` | Modify | Extract Shim<T> helpers to namespace-scope free functions; remove `Shim<T>` template + `NT_HEM_PLUGIN` + `NT_HEM_PAIR` macros + `PairShim` + pair param machinery; add `HemispheresShim` struct + parameters + macro |
| `applets/Hemispheres.cpp` | Create | `NT_HEMISPHERES_PLUGIN(...)` wrapper; the only TU that emits the `_NT_factory` |
| `applets/Logic.cpp` | Modify | Drop `NT_HEM_PLUGIN(...)` macro call; becomes Logic adapter TU (vendor header include + future glue) |
| `applets/AttenuateOffset.cpp` | Modify | Same: drop macro, becomes adapter TU |
| `applets/Slew.cpp` | Modify | Same |
| `applets/Calculate.cpp` | Modify | Same |
| `applets/Burst.cpp` | Modify | Same |
| `applets/LogicCalculate.cpp` | Delete | Pair canary fully retired; no adapter equivalent |
| `Makefile` | Modify | Drop standalone per-applet `.o` plug-in targets; compile adapters as intermediate `.o` files; partial-link them with `Hemispheres_main.o` into `Hemispheres.o` |
| `docs/shim-additions.md` | Modify | Round 6 entry |

---

## Task 1: EmptyApplet + virtual destructor

**Files:**
- Create: `applets/Empty.h`
- Modify: `shim/include/HemisphereApplet.h`

- [ ] **Step 1: Add virtual destructor to HemisphereApplet base**

Locate the closing of the public section in `shim/include/HemisphereApplet.h` (around the existing virtual declarations) and add the destructor:

```cpp
virtual ~HemisphereApplet() = default;
```

Place immediately after the `applet_name()` and `applet_icon()` virtuals, before `Start()`.

- [ ] **Step 2: Write EmptyApplet class**

Create `applets/Empty.h`:

```cpp
#pragma once
#include "HemisphereApplet.h"

class Empty : public HemisphereApplet {
public:
    const char* applet_name() override { return "Empty"; }

    void Start() override {}
    void Controller() override {}
    void View() override {
        gfxPrint(34, 28, "Pick applet");
    }
    uint64_t OnDataRequest() override { return 0; }
    void OnDataReceive(uint64_t) override {}
    void OnEncoderMove(int) override {}

protected:
    void SetHelp() override {}
};
```

`gfxPrint` wrapper applies `HS::gfx_offset` so right-side instance renders at x=34+128.

- [ ] **Step 3: Verify build still passes**

```bash
make arm
```

Expected: all existing per-applet plug-ins still compile (Empty.h not yet included anywhere).

- [ ] **Step 4: Commit**

```bash
git add applets/Empty.h shim/include/HemisphereApplet.h
git -c commit.gpgsign=false commit -m "shim: add Empty applet + virtual dtor on HemisphereApplet base"
```

---

## Task 2: Extract Shim<T> helpers to namespace-scope

**Files:**
- Modify: `shim/include/hem_shim.h`

The Shim<T> template will be removed in Task 5. Its helpers (`copy_bus_to_frame`, `read_gate`, `write_frame_to_bus`) need to survive as free functions. The pair canary (`PairShim`) currently calls `Shim<L>::copy_bus_to_frame(...)`; update it to call namespace-level versions.

- [ ] **Step 1: Add free-function versions inside `hem_shim::`**

Insert after the `kParamCount` enum (around line 23 of current file), before the `Shim<T>` template:

```cpp
inline void copy_bus_to_frame(int bus_param, int* dst, float* busFrames, int numFrames,
                              const int16_t* v) {
    int bus = v[bus_param];
    if (bus <= 0) { *dst = 0; return; }
    const float* src = busFrames + (bus - 1) * numFrames;
    float sum = 0.0f;
    for (int i = 0; i < numFrames; ++i) sum += src[i];
    float mean = sum / (float)numFrames;
    *dst = (int)(mean * 1536.0f);
}

struct GateRead { bool rising; bool high; };
inline GateRead read_gate(int bus_param, float* busFrames, int numFrames, const int16_t* v,
                          bool& prev_high) {
    int bus = v[bus_param];
    if (bus <= 0) { prev_high = false; return { false, false }; }
    const float* src = busFrames + (bus - 1) * numFrames;
    bool rising = false;
    bool last_high = prev_high;
    for (int i = 0; i < numFrames; ++i) {
        bool high = (src[i] > 0.5f);
        if (high && !last_high) rising = true;
        last_high = high;
    }
    prev_high = last_high;
    return { rising, last_high };
}

inline void write_frame_to_bus(int bus_param, int mode_param, int value_hem,
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
```

The inside-`Shim<T>` versions stay temporarily; Task 5 deletes them.

- [ ] **Step 2: Update PairShim references**

In `PairShim::step`, replace every `Shim<L>::copy_bus_to_frame(...)`, `Shim<L>::read_gate(...)`, `Shim<L>::write_frame_to_bus(...)` with bare `copy_bus_to_frame(...)`, `read_gate(...)`, `write_frame_to_bus(...)`. Since these are in `hem_shim::` and `PairShim` is also in `hem_shim::`, no qualification needed.

Replace `Shim<L>::GateRead` with `hem_shim::GateRead` (or just `GateRead` due to namespace context).

- [ ] **Step 3: Verify build**

```bash
make arm
```

Expected: all targets still compile, including LogicCalculate.o pair canary.

- [ ] **Step 4: Commit**

```bash
git add shim/include/hem_shim.h
git -c commit.gpgsign=false commit -m "shim: extract Shim<T> helpers to hem_shim namespace free functions"
```

---

## Task 3: Factory table header

**Files:**
- Create: `shim/include/HemispheresFactory.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
#include "HemisphereApplet.h"
#include "Empty.h"
#include "Logic.h"
#include "AttenuateOffset.h"
#include "Slew.h"
#include "Calculate.h"
#include "Burst.h"

namespace hem_shim {

enum AppletIndex : uint8_t {
    kAppletEmpty = 0,
    kAppletLogic,
    kAppletAttenuateOffset,
    kAppletSlew,
    kAppletCalculate,
    kAppletBurst,
    kAppletCount
};

inline const char* const* applet_enum_strings() {
    static const char* const names[kAppletCount] = {
        "Empty", "Logic", "AttenOff", "Slew", "Calculate", "Burst"
    };
    return names;
}

constexpr size_t kMaxAppletSize = std::max({
    sizeof(Empty),
    sizeof(Logic),
    sizeof(AttenuateOffset),
    sizeof(Slew),
    sizeof(Calculate),
    sizeof(Burst)
});

constexpr size_t kMaxAppletAlign = std::max({
    alignof(Empty),
    alignof(Logic),
    alignof(AttenuateOffset),
    alignof(Slew),
    alignof(Calculate),
    alignof(Burst)
});

template <class T>
inline HemisphereApplet* make_applet(void* sram) {
    return new (sram) T();
}

using AppletFactory = HemisphereApplet* (*)(void*);

inline AppletFactory applet_factory(AppletIndex idx) {
    static const AppletFactory table[kAppletCount] = {
        &make_applet<Empty>,
        &make_applet<Logic>,
        &make_applet<AttenuateOffset>,
        &make_applet<Slew>,
        &make_applet<Calculate>,
        &make_applet<Burst>,
    };
    return table[idx];
}

}  // namespace hem_shim
```

- [ ] **Step 2: Verify it compiles standalone**

Add a temporary `.cpp` test to force instantiation:

```bash
arm-none-eabi-c++ -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
    -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall \
    -Ivendor/distingNT_API/include -Ishim/include \
    -Ivendor/O_C-Phazerville/software/src/applets \
    -x c++ -fsyntax-only - <<EOF
#include "HemispheresFactory.h"
int main() {
    auto sz = hem_shim::kMaxAppletSize;
    auto al = hem_shim::kMaxAppletAlign;
    auto f = hem_shim::applet_factory(hem_shim::kAppletEmpty);
    (void)sz; (void)al; (void)f;
    return 0;
}
EOF
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add shim/include/HemispheresFactory.h
git -c commit.gpgsign=false commit -m "shim: add HemispheresFactory with applet enum, factory table, sram sizes"
```

---

## Task 4: HemispheresShim struct + parameters + machinery

**Files:**
- Modify: `shim/include/hem_shim.h`

Adds the `Hemispheres` plug-in machinery alongside the existing `PairShim`. Old code stays until Task 5 retirement.

- [ ] **Step 1: Add include + param enum + parameter table**

Insert at the top of `hem_shim.h`, after existing includes:

```cpp
#include "HemispheresFactory.h"
```

Insert before `}  // namespace hem_shim` (end of namespace block):

```cpp
// ---------------------------------------------------------------------------
// Hemispheres plug-in: pair-only with runtime applet selectors per side.
// ---------------------------------------------------------------------------

enum {
    kHemSelLeft, kHemSelRight,
    kHemGateInA, kHemGateInB, kHemGateInC, kHemGateInD,
    kHemCvInA,   kHemCvInB,   kHemCvInC,   kHemCvInD,
    kHemCvOutA,  kHemCvOutAMode,
    kHemCvOutB,  kHemCvOutBMode,
    kHemCvOutC,  kHemCvOutCMode,
    kHemCvOutD,  kHemCvOutDMode,
    kHemParamCount
};

inline const _NT_parameter* hemispheres_parameters() {
    static const _NT_parameter params[] = {
        { .name = "Left applet",  .min = 0, .max = kAppletCount - 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = applet_enum_strings() },
        { .name = "Right applet", .min = 0, .max = kAppletCount - 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = applet_enum_strings() },
        NT_PARAMETER_CV_INPUT("Gate (ch A)", 0, 1)
        NT_PARAMETER_CV_INPUT("Gate (ch B)", 0, 2)
        NT_PARAMETER_CV_INPUT("Gate (ch C)", 0, 3)
        NT_PARAMETER_CV_INPUT("Gate (ch D)", 0, 4)
        NT_PARAMETER_CV_INPUT("CV (ch A)",   0, 5)
        NT_PARAMETER_CV_INPUT("CV (ch B)",   0, 6)
        NT_PARAMETER_CV_INPUT("CV (ch C)",   0, 7)
        NT_PARAMETER_CV_INPUT("CV (ch D)",   0, 8)
        NT_PARAMETER_IO("Out (ch A)", 0, 13, kNT_unitCvOutput)
        { .name = "Out (ch A) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
        NT_PARAMETER_IO("Out (ch B)", 0, 14, kNT_unitCvOutput)
        { .name = "Out (ch B) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
        NT_PARAMETER_IO("Out (ch C)", 0, 15, kNT_unitCvOutput)
        { .name = "Out (ch C) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
        NT_PARAMETER_IO("Out (ch D)", 0, 16, kNT_unitCvOutput)
        { .name = "Out (ch D) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
    };
    return params;
}

inline const _NT_parameterPages* hemispheres_parameter_pages() {
    static const uint8_t setup_page[] = { kHemSelLeft, kHemSelRight };
    static const uint8_t routing_page[] = {
        kHemGateInA, kHemGateInB, kHemGateInC, kHemGateInD,
        kHemCvInA,   kHemCvInB,   kHemCvInC,   kHemCvInD,
        kHemCvOutA,  kHemCvOutAMode,
        kHemCvOutB,  kHemCvOutBMode,
        kHemCvOutC,  kHemCvOutCMode,
        kHemCvOutD,  kHemCvOutDMode,
    };
    static const _NT_parameterPage pages[] = {
        { .name = "Setup",   .numParams = sizeof(setup_page),   .params = setup_page },
        { .name = "Routing", .numParams = sizeof(routing_page), .params = routing_page },
    };
    static const _NT_parameterPages parameterPages = {
        .numPages = 2, .pages = pages,
    };
    return &parameterPages;
}
```

- [ ] **Step 2: Add HemispheresInstance struct**

After `hemispheres_parameter_pages()`:

```cpp
struct HemispheresInstance : public _NT_algorithm {
    alignas(kMaxAppletAlign) uint8_t sram_left[kMaxAppletSize];
    alignas(kMaxAppletAlign) uint8_t sram_right[kMaxAppletSize];
    HemisphereApplet* left  = nullptr;
    HemisphereApplet* right = nullptr;
    uint8_t cached_idx_left  = 0;
    uint8_t cached_idx_right = 0;
    bool started = false;
};

inline void hemispheres_reset_side(int side, int offset) {
    HS::frame.outputs[offset + 0].set(0);
    HS::frame.outputs[offset + 1].set(0);
    HS::frame.clock_countdown[offset + 0] = 0;
    HS::frame.clock_countdown[offset + 1] = 0;
    HS::cursor_countdown[side] = 0;
    HS::enc_edit[side].isEditing = false;
}

inline void hemispheres_swap(HemisphereApplet*& slot, void* sram, uint8_t new_idx,
                             HS::HEM_SIDE side) {
    if (slot) slot->~HemisphereApplet();
    int offset = side * 2;
    hemispheres_reset_side(side, offset);
    slot = applet_factory(static_cast<AppletIndex>(new_idx))(sram);
    slot->BaseStart(side);
}
```

- [ ] **Step 3: Add HemispheresShim factory functions**

```cpp
struct HemispheresShim {
    static bool& prev_gate(int idx) {
        static bool prev[4] = { false, false, false, false };
        return prev[idx];
    }

    static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
        req.numParameters = kHemParamCount;
        req.sram = sizeof(HemispheresInstance);
    }

    static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                    const _NT_algorithmRequirements&, const int32_t*) {
        std::memset(ptrs.sram, 0, sizeof(HemispheresInstance));
        auto* alg = new (ptrs.sram) HemispheresInstance();
        alg->parameters     = hemispheres_parameters();
        alg->parameterPages = hemispheres_parameter_pages();
        alg->cached_idx_left  = 0;
        alg->cached_idx_right = 0;
        alg->left  = applet_factory(kAppletEmpty)(alg->sram_left);
        alg->right = applet_factory(kAppletEmpty)(alg->sram_right);
        alg->left->BaseStart(HS::LEFT_HEMISPHERE);
        alg->right->BaseStart(HS::RIGHT_HEMISPHERE);
        alg->started = true;
        return alg;
    }

    static void maybe_swap(HemispheresInstance* alg) {
        uint8_t want_l = (uint8_t)alg->v[kHemSelLeft];
        uint8_t want_r = (uint8_t)alg->v[kHemSelRight];
        if (want_l != alg->cached_idx_left) {
            hemispheres_swap(alg->left, alg->sram_left, want_l, HS::LEFT_HEMISPHERE);
            alg->cached_idx_left = want_l;
        }
        if (want_r != alg->cached_idx_right) {
            hemispheres_swap(alg->right, alg->sram_right, want_r, HS::RIGHT_HEMISPHERE);
            alg->cached_idx_right = want_r;
        }
    }

    static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        maybe_swap(alg);

        int numFrames = numFramesBy4 * 4;
        const int16_t* v = alg->v;

        copy_bus_to_frame(kHemCvInA, &HS::frame.inputs[0], busFrames, numFrames, v);
        copy_bus_to_frame(kHemCvInB, &HS::frame.inputs[1], busFrames, numFrames, v);
        copy_bus_to_frame(kHemCvInC, &HS::frame.inputs[2], busFrames, numFrames, v);
        copy_bus_to_frame(kHemCvInD, &HS::frame.inputs[3], busFrames, numFrames, v);

        { auto g = read_gate(kHemGateInA, busFrames, numFrames, v, prev_gate(0));
          HS::frame.clocked[0] = g.rising; HS::frame.gate_high[0] = g.high; }
        { auto g = read_gate(kHemGateInB, busFrames, numFrames, v, prev_gate(1));
          HS::frame.clocked[1] = g.rising; HS::frame.gate_high[1] = g.high; }
        { auto g = read_gate(kHemGateInC, busFrames, numFrames, v, prev_gate(2));
          HS::frame.clocked[2] = g.rising; HS::frame.gate_high[2] = g.high; }
        { auto g = read_gate(kHemGateInD, busFrames, numFrames, v, prev_gate(3));
          HS::frame.clocked[3] = g.rising; HS::frame.gate_high[3] = g.high; }

        int ticks_this_step = numFrames / 3;
        if (ticks_this_step < 1) ticks_this_step = 1;
        for (int i = 0; i < ticks_this_step; ++i) {
            OC::CORE::ticks += 1;
            for (int ch = 0; ch < 4; ++ch) {
                if (HS::frame.clock_countdown[ch] > 0) {
                    if (--HS::frame.clock_countdown[ch] == 0)
                        HS::frame.outputs[ch].set(0);
                }
            }
            alg->left->Controller();
            alg->right->Controller();
        }

        write_frame_to_bus(kHemCvOutA, kHemCvOutAMode, HS::frame.outputs[0].value,
                           busFrames, numFrames, v);
        write_frame_to_bus(kHemCvOutB, kHemCvOutBMode, HS::frame.outputs[1].value,
                           busFrames, numFrames, v);
        write_frame_to_bus(kHemCvOutC, kHemCvOutCMode, HS::frame.outputs[2].value,
                           busFrames, numFrames, v);
        write_frame_to_bus(kHemCvOutD, kHemCvOutDMode, HS::frame.outputs[3].value,
                           busFrames, numFrames, v);
    }

    static bool draw(_NT_algorithm* self) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        if (!alg->started) return false;
        std::memset(NT_screen, 0, 128 * 64);
        HS::gfx_offset = 0;
        alg->left->View();
        HS::gfx_offset = 128;
        alg->right->View();
        HS::gfx_offset = 0;
        return true;
    }

    static uint32_t hasCustomUi(_NT_algorithm*) {
        return kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR;
    }

    static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        if (data.encoders[0] != 0) alg->left->OnEncoderMove(data.encoders[0]);
        if (data.encoders[1] != 0) alg->right->OnEncoderMove(data.encoders[1]);
        if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
            alg->left->OnButtonPress();
        }
        if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR)) {
            alg->right->OnButtonPress();
        }
    }

    static void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        uint64_t l = alg->left->OnDataRequest();
        uint64_t r = alg->right->OnDataRequest();
        stream.addMemberName("sel_l");        stream.addNumber((int)alg->cached_idx_left);
        stream.addMemberName("sel_r");        stream.addNumber((int)alg->cached_idx_right);
        stream.addMemberName("hem_left_hi");  stream.addNumber((int)(uint32_t)(l >> 32));
        stream.addMemberName("hem_left_lo");  stream.addNumber((int)(uint32_t)(l & 0xFFFFFFFFu));
        stream.addMemberName("hem_right_hi"); stream.addNumber((int)(uint32_t)(r >> 32));
        stream.addMemberName("hem_right_lo"); stream.addNumber((int)(uint32_t)(r & 0xFFFFFFFFu));
    }

    static bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        int num_members = 0;
        if (!parse.numberOfObjectMembers(num_members)) return false;
        int sel_l = -1, sel_r = -1;
        int lhi = 0, llo = 0, rhi = 0, rlo = 0;
        bool got_lhi = false, got_llo = false, got_rhi = false, got_rlo = false;
        for (int i = 0; i < num_members; ++i) {
            if      (parse.matchName("sel_l"))        { if (!parse.number(sel_l)) return false; }
            else if (parse.matchName("sel_r"))        { if (!parse.number(sel_r)) return false; }
            else if (parse.matchName("hem_left_hi"))  { if (!parse.number(lhi)) return false; got_lhi = true; }
            else if (parse.matchName("hem_left_lo"))  { if (!parse.number(llo)) return false; got_llo = true; }
            else if (parse.matchName("hem_right_hi")) { if (!parse.number(rhi)) return false; got_rhi = true; }
            else if (parse.matchName("hem_right_lo")) { if (!parse.number(rlo)) return false; got_rlo = true; }
            else                                      { if (!parse.skipMember()) return false; }
        }
        if (sel_l >= 0 && sel_l < kAppletCount && (uint8_t)sel_l != alg->cached_idx_left) {
            hemispheres_swap(alg->left, alg->sram_left, (uint8_t)sel_l, HS::LEFT_HEMISPHERE);
            alg->cached_idx_left = (uint8_t)sel_l;
        }
        if (sel_r >= 0 && sel_r < kAppletCount && (uint8_t)sel_r != alg->cached_idx_right) {
            hemispheres_swap(alg->right, alg->sram_right, (uint8_t)sel_r, HS::RIGHT_HEMISPHERE);
            alg->cached_idx_right = (uint8_t)sel_r;
        }
        if (got_lhi && got_llo) {
            uint64_t l = ((uint64_t)(uint32_t)lhi << 32) | (uint64_t)(uint32_t)llo;
            alg->left->OnDataReceive(l);
        }
        if (got_rhi && got_rlo) {
            uint64_t r = ((uint64_t)(uint32_t)rhi << 32) | (uint64_t)(uint32_t)rlo;
            alg->right->OnDataReceive(r);
        }
        return true;
    }
};
```

- [ ] **Step 4: Add NT_HEMISPHERES_PLUGIN macro outside the namespace**

After the existing `NT_HEM_PAIR` macro, add:

```cpp
#define NT_HEMISPHERES_PLUGIN(guid_str_4chars, name_str, desc_str) \
    static const _NT_factory _hemispheres_factory = { \
        .guid = NT_MULTICHAR(guid_str_4chars[0], guid_str_4chars[1], \
                             guid_str_4chars[2], guid_str_4chars[3]), \
        .name = name_str, \
        .description = desc_str, \
        .numSpecifications = 0, \
        .calculateRequirements = hem_shim::HemispheresShim::calculateRequirements, \
        .construct             = hem_shim::HemispheresShim::construct, \
        .step                  = hem_shim::HemispheresShim::step, \
        .draw                  = hem_shim::HemispheresShim::draw, \
        .tags                  = kNT_tagUtility, \
        .hasCustomUi           = hem_shim::HemispheresShim::hasCustomUi, \
        .customUi              = hem_shim::HemispheresShim::customUi, \
        .serialise             = hem_shim::HemispheresShim::serialise, \
        .deserialise           = hem_shim::HemispheresShim::deserialise, \
    }; \
    extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) { \
        switch (selector) { \
        case kNT_selector_version:      return kNT_apiVersionCurrent; \
        case kNT_selector_numFactories: return 1; \
        case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &_hemispheres_factory : nullptr); \
        } \
        return 0; \
    }
```

- [ ] **Step 5: Verify all current targets still build**

```bash
make arm
```

Expected: existing per-applet plug-ins, LogicCalculate.o, all still compile.

- [ ] **Step 6: Commit**

```bash
git add shim/include/hem_shim.h
git -c commit.gpgsign=false commit -m "shim: add HemispheresShim with runtime applet selectors and live swap"
```

---

## Task 5: Hemispheres plug-in wrapper, adapter refactor, Makefile partial-link

**Files:**
- Create: `applets/Hemispheres.cpp`
- Modify: `applets/Logic.cpp`, `applets/AttenuateOffset.cpp`, `applets/Slew.cpp`, `applets/Calculate.cpp`, `applets/Burst.cpp`
- Delete: `applets/LogicCalculate.cpp`
- Modify: `Makefile`

- [ ] **Step 1: Write Hemispheres.cpp**

```cpp
#include "hem_shim.h"

NT_HEMISPHERES_PLUGIN("hemi", "Hemispheres",
                     "Phazerville Hemisphere pair: pick two applets")
```

- [ ] **Step 2: Refactor each per-applet wrapper into an adapter TU**

For `applets/Logic.cpp`, replace contents with:

```cpp
#include "hem_shim.h"
#include "Logic.h"

// Logic adapter translation unit.
// Vendor header is inline-only; this TU exists so per-applet compilation
// errors localize here. NT-specific Logic glue (icon overrides, helpers)
// goes here in future work.
```

Apply the analogous edit to:

- `applets/AttenuateOffset.cpp` (include `"AttenuateOffset.h"`)
- `applets/Slew.cpp` (include `"Slew.h"`)
- `applets/Calculate.cpp` (include `"Calculate.h"`)
- `applets/Burst.cpp` (include `"Burst.h"`)

Each file becomes a stub that includes the vendor header and `hem_shim.h`. No `NT_HEM_PLUGIN(...)` invocation. Output `.o` is essentially empty (only inline emissions) and feeds partial linking.

Delete the pair canary:

```bash
git rm applets/LogicCalculate.cpp
```

- [ ] **Step 3: Add adapter list + partial-link rule to Makefile**

In `Makefile`, replace the per-applet `build/arm/Logic.o`, `AttenuateOffset.o`, `Slew.o`, `Calculate.o`, `Burst.o` rules (currently each is a standalone plug-in target) with adapter rules under a new namespaced output dir, and replace `LogicCalculate.o` with the Hemispheres rule:

```makefile
HEMISPHERES_ADAPTERS := \
    build/arm/adapters/Logic.o \
    build/arm/adapters/AttenuateOffset.o \
    build/arm/adapters/Slew.o \
    build/arm/adapters/Calculate.o \
    build/arm/adapters/Burst.o

build/arm/adapters/%.o: applets/%.cpp $(SHIM_DEPS)
	mkdir -p build/arm/adapters
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<

build/arm/Hemispheres_main.o: applets/Hemispheres.cpp $(SHIM_DEPS)
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<

build/arm/Hemispheres.o: build/arm/Hemispheres_main.o $(HEMISPHERES_ADAPTERS)
	arm-none-eabi-ld -r -o $@ $^
```

Update the `arm:` target to drop the old standalone applet `.o` entries and `LogicCalculate.o`, and add `build/arm/Hemispheres.o`:

```makefile
arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/Hemispheres.o
```

- [ ] **Step 4: Build**

```bash
make clean
make arm
```

Expected outputs: `build/arm/gainCustomUI.o`, `build/arm/gain.o`, `build/arm/bus_probe.o`, `build/arm/adapters/*.o`, `build/arm/Hemispheres_main.o`, `build/arm/Hemispheres.o`.

Verify `Hemispheres.o` is a single relocatable object containing the plug-in factory plus all adapter contributions:

```bash
arm-none-eabi-nm build/arm/Hemispheres.o | grep -E "_hemispheres_factory|pluginEntry"
```

Expected: both symbols present.

- [ ] **Step 5: Run host tests**

```bash
make test
```

Expected: gainCustomUI scenario still passes.

- [ ] **Step 6: Commit**

```bash
git add applets/Hemispheres.cpp applets/Logic.cpp applets/AttenuateOffset.cpp \
        applets/Slew.cpp applets/Calculate.cpp applets/Burst.cpp \
        applets/LogicCalculate.cpp Makefile
git -c commit.gpgsign=false commit -m "applets: Hemispheres plug-in + per-applet adapter TUs via partial linking"
```

---

## Task 6: Retire `Shim<T>`, `PairShim`, and legacy macros from `hem_shim.h`

**Files:**
- Modify: `shim/include/hem_shim.h`

The per-applet adapter sources still live in `applets/`, but no TU invokes `NT_HEM_PLUGIN` or `NT_HEM_PAIR` anymore. Strip dead code so future readers don't grep for unused machinery.

- [ ] **Step 1: Remove old machinery from `hem_shim.h`**

Delete:

- The `kParamGateIn1`...`kParamCount` enum block
- `shim_parameters()`, `shim_parameter_pages()` functions
- `AlgorithmInstance<T>` template
- `Shim<T>` template (including the inside-template duplicate helpers, now superseded by namespace-scope versions added in Task 2)
- `kPairGateInA`...`kPairParamCount` enum block
- `pair_parameters()`, `pair_parameter_pages()` functions
- `AlgorithmPairInstance<L, R>` template
- `PairShim<L, R>` template
- `NT_HEM_PLUGIN` macro
- `NT_HEM_PAIR` macro

Keep:

- Includes, `hem_shim::` namespace open/close
- Free helpers: `copy_bus_to_frame`, `read_gate`, `GateRead`, `write_frame_to_bus`
- `HemispheresInstance`, `HemispheresShim`, `hemispheres_parameters`, `hemispheres_parameter_pages`, `hemispheres_reset_side`, `hemispheres_swap`
- `NT_HEMISPHERES_PLUGIN` macro

- [ ] **Step 2: Build**

```bash
make arm
```

Expected: `Hemispheres.o` and the gainCustomUI/gain/bus_probe reference targets build cleanly. No references to the removed templates or macros remain in the codebase.

- [ ] **Step 3: Run host tests**

```bash
make test
```

Expected: gainCustomUI scenario still passes.

- [ ] **Step 4: Commit**

```bash
git add shim/include/hem_shim.h
git -c commit.gpgsign=false commit -m "shim: drop Shim<T>, PairShim, and legacy NT_HEM_PLUGIN/PAIR macros"
```

---

## Task 7: Documentation (Round 6)

**Files:**
- Modify: `docs/shim-additions.md`

- [ ] **Step 1: Add Round 6 section**

Insert before the existing `## Observations` section (after Round 5):

````markdown
## Round 6 (Plan F runtime applet selector)

Replaces all per-applet plug-ins and the LogicCalculate pair canary with a single `Hemispheres` plug-in (GUID `hemi`). Two applet selectors (Left, Right) exposed as enum parameters drive live swap at runtime.

| Change | Why |
|--------|-----|
| Single plug-in `Hemispheres` replaces `Hl01`, `HaO1`, `Hs01`, `HCal`, `Hbst`, `HpLC` | One binary serves all five applets in any pair combination plus single-side-Empty. Drops menu clutter and binary count. |
| Applet selection via parameters, not NT specifications | Specs are add-time only. Parameters let user swap applets without removing + re-adding slot. Preserves UX of live exploration. |
| Empty applet as default + sentinel | Default both sides = Empty. Zero CPU until user picks. Screen shows "Pick applet" hint per side, self-documenting onboarding. |
| Polymorphic `HemisphereApplet*` storage with `kMaxAppletSize` worst-case sram per side | Live swap requires runtime polymorphism. `constexpr std::max({sizeof(...)})` bounds sram per side. |
| Live swap sequence in `step()` head | Detect cached-vs-live selector diff. On change: dtor old, zero side I/O (outputs, clock_countdown, cursor, edit), placement-new new applet, BaseStart, cache new idx. |
| Setup + Routing parameter pages | Setup holds 2 selectors (rare-edit). Routing holds 16 I/O params (frequent-edit). |
| Serialise persists selector indices alongside applet state | Deserialise reconstructs applets first (so state lands in correct class), then feeds 64-bit `OnDataReceive` per side. |
| Retired `Shim<T>`, `NT_HEM_PLUGIN`, `PairShim`, `NT_HEM_PAIR`, pair param machinery | Replaced wholesale. Helpers (`copy_bus_to_frame`, `read_gate`, `write_frame_to_bus`) extracted to `hem_shim::` namespace free functions. |
| Per-applet adapter TUs under `applets/` partial-linked into `Hemispheres.o` | Preserves per-applet compile isolation and gives future per-applet glue (icon overrides, helpers) a natural home. `arm-none-eabi-ld -r` merges adapters with `Hemispheres_main.o`. |

### Sram budget

Each Hemispheres slot allocates `2 * round_up(kMaxAppletSize, kMaxAppletAlign)` bytes for applet storage. Hemisphere applets pack persistent state into a 64-bit blob via `OnDataRequest`; runtime structs add a handful of cursor and CV scratch fields. Total per slot stays sub-KB even at the full ~50-applet enum. No audit needed.

### Flash cost

Every applet linked into `Hemispheres.o` adds its compiled code (typically a few KB per applet). At full enum the plug-in binary is on the order of ~50-150 KB. NT plug-in storage handles this without issue.

### Retired-plug-in artifacts

Pre-Plan-F `.o` files (`Logic.o` etc.) are kept on disk and may still be present on NT devices. They are not rebuilt; `make clean` would remove them. Use the new `Hemispheres.o` going forward.

### Routing collision pitfall (unchanged from Round 5)

Loading multiple `Hemispheres` slots in the same preset still defaults all instances to the same I/O buses. User must re-route per slot.
````

- [ ] **Step 2: Lint**

```bash
markdownlint docs/shim-additions.md
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add docs/shim-additions.md
git -c commit.gpgsign=false commit -m "docs: shim-additions Round 6 documents Hemispheres runtime selector"
```

---

## Task 8: Hardware verification (manual)

This task does not produce code; documents expected device behavior so the user can verify on real NT hardware.

- [ ] **Step 1: Deploy**

```bash
make deploy DEVICE=/Volumes/NT
```

(NT must be in USB disk mode.)

- [ ] **Step 2: Verify behavior table**

After ejecting and rebooting NT, add the `Hemispheres` algorithm to a preset and confirm each row:

| Behavior | Expected |
|----------|----------|
| First-load screen | Two "Pick applet" hints, one per half (x=34 + 0/128, y=28) |
| Setup page param order | Left applet, Right applet |
| Routing page param count | 16 (4 gates, 4 CVs, 4 outs, 4 modes) |
| Change Left applet param to Logic | Left half renders Logic UI; left I/O reads channels A/B (Inputs 1/2 gates, 5/6 CV, Outs 1/2) |
| Change Right applet param to Calculate | Right half renders Calculate UI; right I/O reads channels C/D (Inputs 3/4 gates, 7/8 CV, Outs 3/4) |
| L encoder rotation | Affects left applet only |
| R encoder rotation | Affects right applet only |
| L encoder button | Toggles left applet's cursor/edit mode |
| R encoder button | Toggles right applet's cursor/edit mode |
| Save + restore preset | Selectors and per-applet state both persist |
| Swap Left from Logic to Burst mid-session | Left half re-inits to Burst defaults; left outputs zero briefly during swap |
| Set both selectors to Empty | Both halves show "Pick applet" hint; zero output activity |

- [ ] **Step 3: Record findings**

Update task tracker with results. Note any unexpected behavior for follow-up.

---

## Out of scope

- More than two applets per slot (NT screen is fixed at 256x64; pair already uses both halves).
- Cross-hemisphere routing (left output → right input internally). Each side stays self-contained.
- Tier 2 applet integration (Brancher, TLNeuron, GateDelay). Adding them = one line per applet in `HemispheresFactory.h` once their headers compile clean against the shim.
- Conditional parameter visibility (hide right routing when Right==Empty). NT param API doesn't expose visibility callbacks.

## Risk register

- **Per-applet shim audit required for enum growth:** Tier 1 applets compile clean. Tier 2+ may reference Phazerville-only globals (extended `HSClockManager`, `HSRelabiManager`, `ProbLoopLinker`, etc.) not stubbed in the shim. Each new applet added to the enum needs a compile pass + hardware validation.
- **Live swap during high-rate input:** Swap happens at top of `step()`, before the tick loop. A gate edge that arrived during swap latency is read by the new applet, not the old one. Acceptable per "state reset" semantics.
- **Vtable indirect calls:** `Controller()` and `View()` go through vtable per call. Two indirect calls per tick instead of inlined direct calls. Negligible at NT step rates but worth noting.
- **Deserialise of preset with old selector index out of range:** Defensive check `sel_l < kAppletCount` rejects invalid indices; applets stay at construct-time defaults (Empty).
- **Param edit UX with long enum:** Scrolling through ~50 entries via NT param edit gets tedious. Cosmetic; not blocking. Future polish could group by category.
