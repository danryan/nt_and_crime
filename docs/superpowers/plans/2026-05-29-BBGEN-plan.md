# BBGEN (APP_BBGEN) O_C App Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the vendor `OC::App` app BBGEN (`APP_BBGEN.h`, "Bouncing Balls")
to a disting NT plug-in (GUID `OCBB`) on the O_C apps foundation, as the first
quad-channel app.

**Architecture:** Three Layer 0 shared additions (shim DAC modulation consts,
shim `scope_render`, runtime quad-facade support) land first, baseline-green
between each. Then the per-app unit (`plugins/apps/BBGEN.cpp` + manifest +
Makefile wiring + host test) builds on them. Single PR, sequenced (not parallel:
one cohesive app over shared infra).

**Tech Stack:** C++ (arm-none-eabi-c++ target, clang++/g++ host), Catch2 host
tests, vendor source pinned at `7800d929`, `peaks_resources.cpp` vendor `.cpp`
dep.

**Spec:** `docs/superpowers/specs/2026-05-29-BBGEN-design.md`.

---

## Baseline

- [ ] **Step 0: Confirm clean baseline**

Run: `make test-runtime && make build/host/test_oc_app_Low_rents && ./build/host/test_oc_app_Low_rents`
Expected: PASS. Establishes the OC-app harness is green before changes.

---

## Layer 0.1: Shim DAC modulation constants

**Files:**
- Modify: `shim/include/OC_DAC.h`
- Test: `harness/tests/test_oc_menus.cpp`

- [ ] **Step 1: Write the failing test**

Add to `harness/tests/test_oc_menus.cpp`:

```cpp
TEST_CASE("OC::DAC exposes modulation full-scale constants", "[oc_menus][dac]") {
    // Vendor OC_DAC.h:53,237. NT 16-bit convention: 0V at code 32768, full
    // scale 65535. Modulation apps (BBGEN) bias their unipolar envelope by
    // get_zero_offset and scale against MAX_VALUE.
    REQUIRE(OC::DAC::MAX_VALUE == 65535);
    REQUIRE(OC::DAC::get_zero_offset(DAC_CHANNEL_A) == 32768u);
    REQUIRE(OC::DAC::get_zero_offset(DAC_CHANNEL_D) == 32768u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make build/host/test_oc_menus 2>&1 | tail -20`
Expected: FAIL to compile: `MAX_VALUE`/`get_zero_offset` not members of `OC::DAC`.

- [ ] **Step 3: Implement the constants**

In `shim/include/OC_DAC.h`, inside the `OC::DAC` namespace (near the existing
`set`/history block), add:

```cpp
// Modulation full-scale + zero offset (vendor OC_DAC.h:53,237). NT 16-bit DAC:
// code 32768 == 0V, 65535 == +5V. get_zero_offset ignores the channel (NT has
// no per-channel calibration); the parameter exists for vendor signature
// parity. Modulation apps write get_zero_offset(ch) + value(0..MAX-zero) for a
// unipolar 0V..+5V envelope; the runtime route_cv_output maps it to the bus.
static constexpr uint16_t MAX_VALUE = 65535;
inline uint32_t get_zero_offset(DAC_CHANNEL /*channel*/) { return 32768u; }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make build/host/test_oc_menus && ./build/host/test_oc_menus "[oc_menus][dac]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add shim/include/OC_DAC.h harness/tests/test_oc_menus.cpp
git commit -m "feat(shim): OC::DAC modulation full-scale + zero-offset consts (#42)"
```

---

## Layer 0.2: Shim scope_render

**Files:**
- Modify: `shim/include/OC_menus.h`, `shim/src/oc/menus.cpp`
- Test: `harness/tests/test_oc_menus.cpp`

- [ ] **Step 1: Write the failing test**

Add to `harness/tests/test_oc_menus.cpp`:

```cpp
TEST_CASE("OC::scope_render plots the four-quadrant DAC scope", "[oc_menus][scope]") {
    // Push a distinct value onto each DAC channel's history ring, then render.
    // scope_render averages the ring and plots four quadrants; a non-flat input
    // must light pixels on NT_screen.
    nt::reset_runtime();
    for (int pass = 0; pass < OC::DAC::kHistoryDepth + 4; ++pass) {
        OC::DAC::set(DAC_CHANNEL_A, 10000);
        OC::DAC::set(DAC_CHANNEL_B, 30000);
        OC::DAC::set(DAC_CHANNEL_C, 50000);
        OC::DAC::set(DAC_CHANNEL_D, 60000);
    }
    std::memset(NT_screen, 0, 128 * 64);
    for (int i = 0; i < 64; ++i) OC::scope_render();  // fill the averaged ring
    int lit = 0;
    for (int i = 0; i < 128 * 64; ++i) if (NT_screen[i] != 0) ++lit;
    REQUIRE(lit > 0);
}
```

(Ensure `#include "OC_DAC.h"` and `<cstring>` are present in the test TU; add if
missing.)

- [ ] **Step 2: Run test to verify it fails**

Run: `make build/host/test_oc_menus 2>&1 | tail -20`
Expected: FAIL to compile: `scope_render` not a member of `OC`.

- [ ] **Step 3: Implement scope_render**

In `shim/include/OC_menus.h`, beside `void vectorscope_render();` add:

```cpp
// Four-quadrant DAC output scope (vendor OC_menus.cpp:126). Averages each DAC
// channel's history ring (scope_averaging<11,0x1f>) and plots channels 0..3 in
// the four screen quadrants. Used by modulation-app screensavers (BBGEN).
void scope_render();
```

In `shim/src/oc/menus.cpp`, beside `vectorscope_render`, add (mirroring vendor
`OC_menus.cpp:126-154`, non-Teensy non-NorthernLightModular branch):

```cpp
void scope_render() {
  scope_averaging<11, 0x1f>();
  for (weegfx::coord_t x = 0; x < (weegfx::coord_t)kScopeDepth - 1; ++x) {
    size_t index = (x + averaged_scope_tail + 1) % kScopeDepth;
    graphics.setPixel(x, 0 + averaged_scope_history[0][index]);
    graphics.setPixel(64 + x, 0 + averaged_scope_history[2][index]);
    graphics.setPixel(x, 32 + averaged_scope_history[1][index]);
    graphics.setPixel(64 + x, 32 + averaged_scope_history[3][index]);
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make build/host/test_oc_menus && ./build/host/test_oc_menus "[oc_menus][scope]"`
Expected: PASS. Then run the full binary: `./build/host/test_oc_menus`
Expected: all PASS (existing vectorscope test unaffected).

- [ ] **Step 5: Commit**

```bash
git add shim/include/OC_menus.h shim/src/oc/menus.cpp harness/tests/test_oc_menus.cpp
git commit -m "feat(shim): OC::scope_render four-quadrant DAC scope (#42)"
```

---

## Layer 0.3: Runtime quad-facade support

Pure, behavior-preserving refactor of `_per_app_runtime.h`. Guarded by the
existing OC-app tests (Low_rents, Harrington1200, FPART, StubApp).

**Files:**
- Modify: `plugins/apps/_per_app_runtime.h`

- [ ] **Step 1: Add the name-override hook to SettingsFacade**

In the `SettingsFacade` struct (after `value_attr_at`), add:

```cpp
    // Optional per-row name override. When non-null, construct uses
    // param_name(instance, idx) as the NT parameter name instead of the
    // value_attr name. Quad-channel apps (BBGEN) use it for "A Gravity" etc.
    const char* (*param_name)(void* self, int idx) = nullptr;
```

- [ ] **Step 2: Add a name override to param_from_value_attr**

Change `param_from_value_attr` to accept an optional override:

```cpp
inline _NT_parameter param_from_value_attr(const settings::value_attr& va,
                                           const char* name_override = nullptr) {
    _NT_parameter p{};
    p.name = name_override != nullptr ? name_override : va.name;
    p.min  = static_cast<int16_t>(va.min_);
    p.max  = static_cast<int16_t>(va.max_);
    p.def  = static_cast<int16_t>(va.default_);
    if (va.value_names != nullptr) {
        p.unit         = kNT_unitEnum;
        p.enumStrings  = va.value_names + va.min_;
    } else {
        p.unit         = kNT_unitNone;
        p.enumStrings  = nullptr;
    }
    p.scaling = 0;
    return p;
}
```

- [ ] **Step 3: Extract construct_with_facade and rewire templated construct**

Replace the templated `construct(alg, app, settings, num_settings)` body with a
delegation, and add the non-template `construct_with_facade`:

```cpp
// Construct with a prebuilt facade (single-instance or quad). Populates the
// settings parameter rows from the facade, applying any param_name override.
inline void construct_with_facade(AppAlgorithm& alg, const OC::App* app,
                                  SettingsFacade facade, int num_settings) {
    construct(alg, app);
    alg.settings_facade = facade;
    alg.settings_facade.num_settings = num_settings;
    for (int i = 0; i < num_settings; ++i) {
        const auto* va = alg.settings_facade.value_attr_at(i);
        const char* nm = alg.settings_facade.param_name != nullptr
            ? alg.settings_facade.param_name(alg.settings_facade.instance, i)
            : nullptr;
        alg.parameters_storage[kIoParamCount + i] =
            param_from_value_attr(*va, nm);
        alg.v_storage[kIoParamCount + i] =
            static_cast<int16_t>(alg.settings_facade.get_value(
                alg.settings_facade.instance, i));
    }
}

template <typename Settings>
inline void construct(AppAlgorithm& alg, const OC::App* app,
                      Settings* settings, int num_settings) {
    construct_with_facade(alg, app, make_facade(settings), num_settings);
}
```

- [ ] **Step 4: Verify the refactor preserves behavior**

Run: `make test-runtime && for a in StubApp Low_rents Harrington1200 FPART; do make build/host/test_oc_app_$a && ./build/host/test_oc_app_$a || exit 1; done`
Expected: all PASS (the refactor is behavior-preserving for single-instance apps).

- [ ] **Step 5: Commit**

```bash
git add plugins/apps/_per_app_runtime.h
git commit -m "refactor(oc-apps): construct_with_facade + param_name hook for quad apps (#42)"
```

---

## Layer 1: BBGEN per-app unit

### Task 4: Failing registration test

**Files:**
- Create: `harness/tests/test_oc_app_BBGEN.cpp`

- [ ] **Step 1: Write the failing test (registration only first)**

Create `harness/tests/test_oc_app_BBGEN.cpp`:

```cpp
// BBGEN (vendor APP_BBGEN.h) O_C app port. First quad-channel app: the app
// object is QuadBouncingBalls (4 BouncingBall SettingsBase instances), so the
// settings facade is a quad facade dispatching 44 logical rows across the four
// balls (idx/11 = channel, idx%11 = setting). Validates the real app through the
// firmware factory lifecycle via the shared plugin_loader.
//
// Only plugins/apps/BBGEN.cpp (NT_OC_APP_TU) aggregates the OC shim impl; this
// TU links it and reaches the embedded state through the test seams below.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "oc_ui_sim.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_core.h"

#include <cstring>
#include <string>

// Test seams defined in plugins/apps/BBGEN.cpp.
int  bbgen_get_setting(int channel, int setting);
bool bbgen_apply_setting(int channel, int setting, int value);
int  bbgen_setting_count();          // 4 * BB_SETTING_LAST == 44
int  bbgen_settings_per_channel();   // BB_SETTING_LAST == 11
int  bbgen_settings_param_base();
const char* bbgen_param_name(int idx);  // channel-prefixed NT row name
void bbgen_arm_sentinel(_NT_algorithm* self);

namespace {
enum {
    BB_GRAVITY = 0, BB_BOUNCE_LOSS, BB_INITIAL_AMPLITUDE, BB_INITIAL_VELOCITY,
    BB_TRIGGER_INPUT, BB_RETRIGGER_BOUNCES, BB_CV1, BB_CV2, BB_CV3, BB_CV4,
    BB_HARD_RESET, BB_LAST,
};
void run_steps(nt::LoadedPlugin* p, int numFrames, int steps) {
    for (int s = 0; s < steps; ++s)
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
}
}  // namespace

TEST_CASE("BBGEN loads through the factory path with a custom UI", "[oc_app][bbgen][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);
    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'B', 'B'));
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);

    REQUIRE(bbgen_settings_per_channel() == BB_LAST);
    REQUIRE(BB_LAST == 11);
    REQUIRE(bbgen_setting_count() == 44);
}

TEST_CASE("BBGEN exposes 56 parameters with channel-prefixed setting names", "[oc_app][bbgen][params]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // 12 I/O routing rows + 44 settings.
    REQUIRE(p->algorithm->parameters[bbgen_settings_param_base()].name != nullptr);
    // Row 0 of the settings block is channel A, setting GRAVITY -> "A Gravity".
    REQUIRE(std::string(bbgen_param_name(0)) == "A Gravity");
    // Channel B, GRAVITY -> "B Gravity".
    REQUIRE(std::string(bbgen_param_name(BB_LAST)) == "B Gravity");
    // Channel D, HARD_RESET -> "D Hard reset" (last row).
    REQUIRE(std::string(bbgen_param_name(43)) == "D Hard reset");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make build/host/test_oc_app_BBGEN 2>&1 | tail -20`
Expected: FAIL (no `plugins/apps/BBGEN.cpp`, unresolved seams, GUID OCBB
unknown).

### Task 5: Manifest, per-app source, Makefile wiring

**Files:**
- Create: `shim/include/oc_app_manifests/BBGEN.h`
- Create: `plugins/apps/BBGEN.cpp`
- Modify: `Makefile`

- [ ] **Step 3a: Create the manifest**

Create `shim/include/oc_app_manifests/BBGEN.h`:

```cpp
#pragma once
// Vendor app: APP_BBGEN.h ("Bouncing Balls": four peaks bouncing-ball envelope
// generators, by Tim Churches). First quad-channel OC::App port.
//
// O_C-app manifest. The per-app runtime emits a fixed 12-row I/O routing block
// (oc_runtime::emit_io_params). These BusParam names document the vendor wiring;
// the runtime emits its own generic row names ("CV in 1" etc.):
//   CV in 1..4 -> the four ADC channels the ISR smooths (APP_BBGEN.h:250-253),
//     each routable to a ball parameter via the per-ball CV1..CV4 mapping.
//   CV out A..D -> the four ball envelopes (DAC_CHANNEL_A..D, APP_BBGEN.h:259).
//   TR in 1..4 -> DIGITAL_INPUT_1..4; each ball's "Trigger input" setting picks
//     which one gates it (APP_BBGEN.h:171-181).
//
// guid uses the "OC" prefix (shipped: OCLR, OCHA, OCSb, OCFP); OCBB is unique.
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct BBGEN {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'B', 'B');
    static constexpr const char* name        = "Bouncing Balls";
    static constexpr const char* description = "Four bouncing-ball envelope generators (O_C APP_BBGEN port)";

    static constexpr BusParam inputs[] = {
        {"CV in 1", BusKind::cv}, {"CV in 2", BusKind::cv},
        {"CV in 3", BusKind::cv}, {"CV in 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"Ball A", BusKind::cv}, {"Ball B", BusKind::cv},
        {"Ball C", BusKind::cv}, {"Ball D", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"Trig 1", BusKind::gate}, {"Trig 2", BusKind::gate},
        {"Trig 3", BusKind::gate}, {"Trig 4", BusKind::gate},
    };
};
}  // namespace oc_app
```

- [ ] **Step 3b: Create the per-app source**

Create `plugins/apps/BBGEN.cpp` (structure mirrors `plugins/apps/Low_rents.cpp`;
the divergence is the quad facade + name builder + `construct_with_facade`):

```cpp
// BBGEN: O_C APP_BBGEN port (four peaks bouncing-ball envelope generators).
//
// First quad-channel OC::App. The vendor app object is the file-scope singleton
// QuadBouncingBalls `bbgen`, holding BouncingBall balls_[4]. Each ball is a
// SettingsBase<BouncingBall, BB_SETTING_LAST> with 11 settings. The NT plug-in
// exposes all 4*11 = 44 settings as flat parameter rows; the settings facade is
// a quad facade dispatching row idx to balls_[idx/11] setting idx%11. Names are
// channel-prefixed ("A Gravity" ... "D Hard reset") since the flat NT param page
// has no channel grouping (the vendor customUI keeps the A/B/C/D titlebar UX).
#define NT_OC_APP_TU 1

#include "_per_app_runtime.h"

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "OC_config.h"
#include "OC_menus.h"
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "util/util_math.h"
#include "UI/ui_events.h"

#include "../../shim/include/oc_app_manifests/BBGEN.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

// Bind bare `menu::` (used by the vendor app body) to OC::menu without dragging
// OC::UI into global scope (see Low_rents.cpp for the rationale).
namespace menu = OC::menu;

#define ENABLE_APP_BBGEN 1
#include "APP_BBGEN.h"

namespace {

using ManifestNS = oc_app::BBGEN;

struct BBGENInstance : public oc_runtime::AppAlgorithm {};
BBGENInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_bbgen_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              BBGEN_init,
    /* storageSize */       BBGEN_storageSize,
    /* Save */              BBGEN_save,
    /* Restore */           BBGEN_restore,
    /* HandleAppEvent */    BBGEN_handleAppEvent,
    /* loop */              BBGEN_loop,
    /* DrawMenu */          BBGEN_menu,
    /* DrawScreensaver */   BBGEN_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(BBGEN_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(BBGEN_handleEncoderEvent),
    /* isr */               BBGEN_isr,
};

// Channel-prefixed parameter names. Filled once at construct. The NT parameter
// .name pointer must outlive construct; this file-scope static satisfies it.
// Multiple instances share it (names are identical), so no per-instance state.
constexpr int kNumChannels = 4;
constexpr int kNumSettings = kNumChannels * BB_SETTING_LAST;  // 44
char g_names[kNumSettings][16];

void build_names() {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int s = 0; s < BB_SETTING_LAST; ++s) {
            const int i = ch * BB_SETTING_LAST + s;
            const char* vn = BouncingBall::value_attr(static_cast<size_t>(s)).name;
            char* dst = g_names[i];
            dst[0] = static_cast<char>('A' + ch);
            dst[1] = ' ';
            size_t len = std::strlen(vn);
            if (len > 13) len = 13;  // 16 - 2 prefix - 1 null
            std::memcpy(dst + 2, vn, len);
            dst[2 + len] = '\0';
        }
    }
}

// The quad facade. instance is the vendor `bbgen` singleton; the lambdas are
// captureless (they reference file-scope globals: bbgen and the BBGEN_* quad
// persistence thunks), so each is a plain function pointer.
oc_runtime::SettingsFacade make_quad_facade() {
    oc_runtime::SettingsFacade f;
    f.instance = &bbgen;
    f.num_settings = kNumSettings;  // overwritten by construct_with_facade
    f.get_value = [](void* self, int idx) -> int {
        return static_cast<QuadBouncingBalls*>(self)
            ->balls_[idx / BB_SETTING_LAST]
            .get_value(static_cast<size_t>(idx % BB_SETTING_LAST));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        return static_cast<QuadBouncingBalls*>(self)
            ->balls_[idx / BB_SETTING_LAST]
            .apply_value(static_cast<size_t>(idx % BB_SETTING_LAST), value);
    };
    f.save = [](void* /*self*/, void* blob) -> size_t { return BBGEN_save(blob); };
    f.restore = [](void* /*self*/, const void* blob) -> size_t { return BBGEN_restore(blob); };
    f.storage_size = []() -> size_t { return BBGEN_storageSize(); };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &BouncingBall::value_attr(static_cast<size_t>(idx % BB_SETTING_LAST));
    };
    f.param_name = [](void* /*self*/, int idx) -> const char* {
        return g_names[idx];
    };
    return f;
}

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // 12 I/O routing rows + 44 settings (CLAUDE.md numParameters gotcha).
    req.numParameters = oc_runtime::kIoParamCount + kNumSettings;  // 56
    req.sram = sizeof(BBGENInstance);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) BBGENInstance();
    g_instance = inst;
    build_names();
    // BBGEN_init() (app->Init, fired inside construct) calls bbgen.Init(), which
    // InitDefaults() every ball. Wire the quad facade and build the parameter
    // table (I/O routing + 44 settings), then seed v[] from post-default values.
    oc_runtime::construct_with_facade(*inst, &the_bbgen_app, make_quad_facade(),
                                      kNumSettings);
    return inst;
}

void emit_button(const BBGENInstance* inst, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void emit_encoder(const BBGENInstance* inst, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void push_settings_to_params(BBGENInstance* inst) {
    if (!inst->alive) return;
    const int32_t idx = NT_algorithmIndex(inst);
    if (idx < 0) return;
    const int base = oc_runtime::settings_param_base();
    const int n = inst->settings_facade.num_settings;
    for (int s = 0; s < n; ++s) {
        const int v = inst->settings_facade.get_value(inst->settings_facade.instance, s);
        if (inst->v[base + s] != static_cast<int16_t>(v)) {
            // NT_setParameterFromUi takes the GLOBAL index: add
            // NT_parameterOffset() (CLAUDE.md offset gotcha).
            NT_setParameterFromUi(static_cast<uint32_t>(idx),
                                  static_cast<uint32_t>(base + s) + NT_parameterOffset(),
                                  static_cast<int16_t>(v));
        }
    }
}

void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    auto* inst = static_cast<BBGENInstance*>(self);
    if (!inst->app) return;

    int n = 0;
    const oc_runtime::ControlMapping* tbl = oc_runtime::button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;
    for (int i = 0; i < n; ++i) {
        const uint16_t bit = tbl[i].nt_bit;
        const int bi = oc_runtime::bit_index(bit);
        const bool now_down = (data.controls & bit) != 0;
        if ((edges & bit) && !now_down) {
            const uint8_t ev = oc_runtime::classify_release(inst, bi);
            emit_button(inst, tbl[i].oc_control, ev);
        }
    }
    if (data.encoders[0] != 0) emit_encoder(inst, OC::CONTROL_ENCODER_L, data.encoders[0]);
    if (data.encoders[1] != 0) emit_encoder(inst, OC::CONTROL_ENCODER_R, data.encoders[1]);

    push_settings_to_params(inst);
    oc_runtime::customUi(*inst, data);
}

const _NT_factory factory = {
    .guid        = ManifestNS::guid,
    .name        = ManifestNS::name,
    .description = ManifestNS::description,
    .calculateRequirements = calculateRequirements_impl,
    .construct             = construct_impl,
    .parameterChanged      = oc_runtime::parameterChanged_factory,
    .step                  = oc_runtime::step_factory,
    .draw                  = oc_runtime::draw_factory,
    .tags                  = kNT_tagUtility,
    .hasCustomUi           = oc_runtime::hasCustomUi_factory,
    .customUi              = customUi_impl,
    .serialise             = oc_runtime::serialise_factory,
    .deserialise           = oc_runtime::deserialise_factory,
};

}  // namespace

extern "C" __attribute__((visibility("default")))
uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&factory : 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Test seams. The vendor QuadBouncingBalls type and `bbgen` singleton are only
// visible in this TU.
// ---------------------------------------------------------------------------
int bbgen_get_setting(int channel, int setting) {
    return bbgen.balls_[channel].get_value(static_cast<size_t>(setting));
}
bool bbgen_apply_setting(int channel, int setting, int value) {
    return bbgen.balls_[channel].apply_value(static_cast<size_t>(setting), value);
}
int bbgen_setting_count() { return kNumSettings; }
int bbgen_settings_per_channel() { return BB_SETTING_LAST; }
int bbgen_settings_param_base() { return oc_runtime::settings_param_base(); }
const char* bbgen_param_name(int idx) { build_names(); return g_names[idx]; }
void bbgen_arm_sentinel(_NT_algorithm* self) {
    static_cast<BBGENInstance*>(self)->alive = true;
}
```

- [ ] **Step 3c: Wire the Makefile**

In `Makefile`: add `BBGEN` to `OC_APP_LIST`:

```make
OC_APP_LIST := StubApp Low_rents Harrington1200 FPART BBGEN
```

Add the vendor-dep variables near the other `VENDOR_DEPS_*`/
`VENDOR_DEP_HOST_SRCS_*` OC-app lines (Low_rents is the precedent):

```make
VENDOR_DEPS_BBGEN          := build/arm/vendor_src/peaks_resources.o
VENDOR_DEP_HOST_SRCS_BBGEN := $(HEM_SRC_DIR)/peaks_resources.cpp
```

- [ ] **Step 4: Run the registration tests**

Run: `make build/host/test_oc_app_BBGEN && ./build/host/test_oc_app_BBGEN "[oc_app][bbgen][factory],[oc_app][bbgen][params]"`
Expected: PASS (GUID OCBB, 44 settings, channel-prefixed names).

- [ ] **Step 5: Commit**

```bash
git add shim/include/oc_app_manifests/BBGEN.h plugins/apps/BBGEN.cpp Makefile harness/tests/test_oc_app_BBGEN.cpp
git commit -m "feat(apps): BBGEN quad facade, manifest, build wiring + registration tests (#42)"
```

### Task 6: Quad-facade routing test

**Files:**
- Modify: `harness/tests/test_oc_app_BBGEN.cpp`

- [ ] **Step 1: Write the failing test**

Add:

```cpp
TEST_CASE("BBGEN parameter rows route to the correct ball and setting", "[oc_app][bbgen][routing]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    bbgen_arm_sentinel(p->algorithm);
    const int base = bbgen_settings_param_base();

    // Direction NT -> app: writing global row (base + ch*11 + setting) then
    // firing parameterChanged updates ONLY balls_[ch] setting, no other channel.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    const int ch = 2, setting = BB_GRAVITY;  // "C Gravity"
    const int row = ch * BB_LAST + setting;
    const int other_before = bbgen_get_setting(0, BB_GRAVITY);
    v[base + row] = 77;
    p->factory->parameterChanged(p->algorithm, base + row);
    REQUIRE(bbgen_get_setting(ch, setting) == 77);
    REQUIRE(bbgen_get_setting(0, BB_GRAVITY) == other_before);  // channel A untouched
}
```

- [ ] **Step 2: Run to verify it fails / passes**

Run: `make build/host/test_oc_app_BBGEN && ./build/host/test_oc_app_BBGEN "[oc_app][bbgen][routing]"`
Expected: PASS (the quad facade idx/11, idx%11 mapping is already implemented;
this test locks the behavior in). If FAIL, fix the facade indexing.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_oc_app_BBGEN.cpp
git commit -m "test(apps): BBGEN quad-facade row routing (#42)"
```

### Task 7: Output (envelope) test

**Files:**
- Modify: `harness/tests/test_oc_app_BBGEN.cpp`

- [ ] **Step 1: Write the failing test**

Add:

```cpp
TEST_CASE("BBGEN gate-triggered envelope outputs within 0V..+5V and moves", "[oc_app][bbgen][isr]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Ball A's default trigger input is DIGITAL_INPUT_1 -> TR in 1 -> default
    // bus 5. Raise it high to gate ball A; the unipolar envelope rises then
    // decays. Ball A -> CV out A -> default bus 13.
    float* trig1 = nt::bus_pointer(5, numFrames);
    float* outA  = nt::bus_pointer(13, numFrames);
    for (int i = 0; i < numFrames; ++i) trig1[i] = 5.0f;

    // Run enough buffers for the envelope to develop.
    run_steps(p, numFrames, 40);

    // Output must move (envelope is dynamic, not a static constant) and stay
    // within the unipolar 0V..+5V rails (modulation code space, not railed
    // full-scale by a wrong /1536 conversion).
    const float sample0 = outA[0];
    bool moved = false;
    for (int s = 0; s < 80 && !moved; ++s) {
        run_steps(p, numFrames, 1);
        if (outA[0] != Catch::Approx(sample0).margin(1e-6)) moved = true;
    }
    REQUIRE(moved);
    for (int s = 0; s < 80; ++s) {
        run_steps(p, numFrames, 1);
        REQUIRE(outA[0] >= -0.1f);
        REQUIRE(outA[0] <= 5.1f);
    }
}
```

- [ ] **Step 2: Run**

Run: `make build/host/test_oc_app_BBGEN && ./build/host/test_oc_app_BBGEN "[oc_app][bbgen][isr]"`
Expected: PASS. If output is railed or static, revisit the DAC scaling
(L0.1 zero_offset/MAX_VALUE) or the trigger routing default.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_oc_app_BBGEN.cpp
git commit -m "test(apps): BBGEN envelope output range + movement (#42)"
```

### Task 8: Serialise round-trip test

**Files:**
- Modify: `harness/tests/test_oc_app_BBGEN.cpp`

- [ ] **Step 1: Write the failing test**

Add:

```cpp
TEST_CASE("BBGEN settings round-trip through factory serialise/deserialise", "[oc_app][bbgen][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Write a distinct in-range value into every channel's GRAVITY and
    // BOUNCE_LOSS (U8 0..255), enough to prove the 4-ball blob round-trips.
    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bbgen_apply_setting(ch, BB_GRAVITY, 50 + ch * 10));
        REQUIRE(bbgen_apply_setting(ch, BB_BOUNCE_LOSS, 200 - ch * 10));
    }

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    // Clobber, then restore.
    for (int ch = 0; ch < 4; ++ch) {
        bbgen_apply_setting(ch, BB_GRAVITY, 0);
        bbgen_apply_setting(ch, BB_BOUNCE_LOSS, 0);
    }
    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bbgen_get_setting(ch, BB_GRAVITY) == 50 + ch * 10);
        REQUIRE(bbgen_get_setting(ch, BB_BOUNCE_LOSS) == 200 - ch * 10);
    }
}
```

- [ ] **Step 2: Run**

Run: `make build/host/test_oc_app_BBGEN && ./build/host/test_oc_app_BBGEN "[oc_app][bbgen][settings]"`
Expected: PASS (4-ball blob = 36 bytes, under kMaxBlobBytes 512).

- [ ] **Step 3: Run the full BBGEN binary + the OC-app suite**

Run: `./build/host/test_oc_app_BBGEN && make test-applets`
Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
git add harness/tests/test_oc_app_BBGEN.cpp
git commit -m "test(apps): BBGEN 4-ball serialise round-trip (#42)"
```

### Task 9: ARM build + size/symbol verification

- [ ] **Step 1: Build the ARM plug-in**

Run: `make build/arm/BBGEN.o 2>&1 | tail -20`
Expected: builds clean (peaks_resources.o linked in).

- [ ] **Step 2: Verify no unexpected unresolved symbols**

Run: `arm-none-eabi-nm build/arm/BBGEN.o | grep ' U ' | sort`
Expected: only the firmware-resolved surface (NT_* ABI,
`_GLOBAL_OFFSET_TABLE_`, newlib memcpy/memset/memmove/strlen/strcmp/logf/powf,
compiler-rt EABI aliases). No `lut_gravity`, no `_ZN5peaks*` unresolved (proves
peaks_resources linked). If `lut_gravity` is unresolved, the VENDOR_DEPS link is
missing.

- [ ] **Step 3: Verify the .text size is well under the cap**

Run: `arm-none-eabi-size build/arm/BBGEN.o`
Expected: `.text` well under the ~82 KB firmware cap (per-app apps run 11-20 KB).

- [ ] **Step 4: Full ARM build regression**

Run: `make arm 2>&1 | tail -5`
Expected: all plug-ins build, including the existing set (no regression from the
runtime refactor or shim additions).

- [ ] **Step 5: Commit (if any build-only fixes were needed)**

```bash
git add -A && git commit -m "build(apps): BBGEN ARM build clean (#42)" || echo "nothing to commit"
```

---

## PR

- [ ] **Step 1: Markdownlint the docs**

Run: `markdownlint docs/superpowers/specs/2026-05-29-BBGEN-design.md docs/superpowers/plans/2026-05-29-BBGEN-plan.md`
Expected: clean.

- [ ] **Step 2: Push and open the PR**

Detect target branch, push the feature branch, open a PR titled
`feat(apps): port BBGEN (APP_BBGEN) O_C app to NT plug-in (#42)` with a
Summary (what/why/how: first quad-channel OC::App, quad facade + DAC modulation
helpers as reusable Layer 0 for BYTEBEATGEN, peaks_resources.cpp link
correction) and a Test plan checklist (host tests for registration, quad
routing, envelope range, serialise round-trip; ARM build clean; nm/size checks).

- [ ] **Step 3: Hardware ADD smoke check (after PR open, needs physical NT)**

Deploy via `make deploy-sysex SYSEX_PLUGIN=build/arm/BBGEN.o SYSEX_ID=0`, then
`mcp__nt_helper__new` with GUID OCBB to confirm it ADDs (not just registers).
Tracked in #55. Verify envelope output on a scope. This is the only step the
host suite cannot prove.

---

## Self-review notes

- Spec coverage: L0.1 (DAC consts) -> Task L0.1; L0.2 (scope_render) -> L0.2;
  L0.3 (quad facade + name override + construct_with_facade) -> L0.3; per-app
  unit, manifest, build wiring, vendor .cpp link -> Tasks 4-5; quad routing ->
  Task 6; output range -> Task 7; round-trip -> Task 8; size/symbol -> Task 9.
  Channel-prefixed names -> Task 4 (params test) + Task 5 (build_names).
- Vendor .cpp link correction (peaks_resources) -> Task 5 Makefile + Task 9 nm.
- 10x multiplier: the envelope is continuous sample output, so Task 7 asserts
  movement/range, not per-edge fire count (spec test-focus honored).
- Hardware ADD hazards (serialise addNumber-only, sr==0 guard) already in the
  runtime; not re-implemented.
