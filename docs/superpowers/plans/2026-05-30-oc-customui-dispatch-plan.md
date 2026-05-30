# O_C customUI dispatch consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace five copy-pasted O_C per-app customUI dispatch blocks with one shared header so each app's `customUi` collapses to a single factory line.

**Architecture:** New sibling header `plugins/apps/oc_customui_dispatch.h` (namespace `oc_runtime`) owns event construction, the edge loop, short/long classification, and the NT parameter push-back. It includes vendor `UI/ui_events.h`, confining that coupling to the one file that builds events; core `_per_app_runtime.h` and `harness/include/oc_ui_sim.h` stay UI-free. Each per-app `.cpp` deletes its local glue and sets `.customUi = oc_runtime::dispatch_custom_ui_factory<bool>`.

**Tech Stack:** C++17 (host tests, clang/g++), C++20 (ARM, arm-none-eabi-g++), Catch2, Make.

**Spec:** `docs/superpowers/specs/2026-05-30-oc-customui-dispatch-design.md`

---

## File structure

- Create: `plugins/apps/oc_customui_dispatch.h` - the shared dispatch (emit_button, emit_encoder, push_settings_to_params, dispatch_custom_ui, dispatch_custom_ui_factory).
- Create: `harness/tests/test_oc_dispatch.cpp` - unit test driving `dispatch_custom_ui` against a recording stub app.
- Modify: `Makefile` - add the `test_oc_dispatch` host target.
- Modify: `plugins/apps/Low_rents.cpp`, `Harrington1200.cpp`, `FPART.cpp`, `BBGEN.cpp`, `BYTEBEATGEN.cpp` - delete local glue, add include, set factory, fix test seams.

All dispatch functions are `inline` (header-only, may be pulled by one TU per test binary; inline keeps it ODR-safe and lets `-O2` inline them into `customUi` exactly as the current file-local statics are, preserving emitted code).

---

### Task 1: Failing unit test for `dispatch_custom_ui`

**Files:**
- Create: `harness/tests/test_oc_dispatch.cpp`
- Modify: `Makefile` (add target after the `test-oc-router` block, around line 629)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_oc_dispatch.cpp`:

```cpp
// Unit test for the consolidated O_C customUI dispatch
// (plugins/apps/oc_customui_dispatch.h). Drives dispatch_custom_ui against a
// recording stub OC::App and asserts the emitted (type, control) per gesture,
// for both map_long_press variants. Push-back is a no-op here (alg.alive stays
// false), isolating the emit path.

#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "OC_core.h"
#include "OC_config.h"
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "UI/ui_events.h"

#include "../../plugins/apps/oc_customui_dispatch.h"
#include "oc_ui_sim.h"

#include <cstdint>

namespace {

using oc_runtime::AppAlgorithm;

// Recorded last button / encoder event. The OC::App handler receives a
// const OC::UI::Event& (forward-declared incomplete); reinterpret_cast to the
// real ::UI::Event (layout-identical) to read its fields, the same bridge the
// per-app TUs use.
struct Rec {
    int btn_type = -1, btn_control = -1;
    int enc_control = -1, enc_value = 0;
    int btn_count = 0, enc_count = 0;
};
Rec& rec() { static Rec r; return r; }

void rec_button(const OC::UI::Event& e) {
    const auto& ev = reinterpret_cast<const ::UI::Event&>(e);
    rec().btn_type = ev.type;
    rec().btn_control = ev.control;
    rec().btn_count++;
}
void rec_encoder(const OC::UI::Event& e) {
    const auto& ev = reinterpret_cast<const ::UI::Event&>(e);
    rec().enc_control = ev.control;
    rec().enc_value = ev.value;
    rec().enc_count++;
}

void s_init() {}
size_t s_storage_size() { return 0; }
size_t s_save(void*) { return 0; }
size_t s_restore(const void*) { return 0; }
void s_app_event(OC::AppEvent) {}
void s_loop() {}
void s_draw_menu() {}
void s_draw_ss() {}
void s_isr() {}

const OC::App* make_rec_app() {
    static OC::App app = {
        0xB1B1, "Rec", s_init, s_storage_size, s_save, s_restore, s_app_event,
        s_loop, s_draw_menu, s_draw_ss, rec_button, rec_encoder, s_isr,
    };
    return &app;
}

// Drive a down edge, advance ticks, then an up edge on `bit` through
// dispatch_custom_ui. Returns nothing; read rec() after.
void press_release(AppAlgorithm& alg, uint16_t bit, uint64_t hold_ticks,
                   bool map_long_press) {
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(bit, 0, 0, 0), map_long_press);
    OC::CORE::ticks += hold_ticks;
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(0, bit, 0, 0), map_long_press);
}

void setup(AppAlgorithm& alg) {
    OC::CORE::ticks = 0;
    nt::reset_runtime();
    oc_runtime::construct(alg, make_rec_app());
    rec() = Rec{};
}

}  // namespace

TEST_CASE("dispatch: short release emits PRESS", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button3, oc_ui_sim::kShortHoldTicks, /*map*/ false);
    REQUIRE(rec().btn_count == 1);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_PRESS);
    REQUIRE(rec().btn_control == OC::CONTROL_BUTTON_UP);  // kNT_button3 -> UP
}

TEST_CASE("dispatch: long release with map=false emits LONG_RELEASE", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button4, oc_ui_sim::kLongHoldTicks, /*map*/ false);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_LONG_RELEASE);
}

TEST_CASE("dispatch: long release with map=true emits LONG_PRESS", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button4, oc_ui_sim::kLongHoldTicks, /*map*/ true);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_LONG_PRESS);
}

TEST_CASE("dispatch: short release with map=true emits PRESS", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button3, oc_ui_sim::kShortHoldTicks, /*map*/ true);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_PRESS);
}

TEST_CASE("dispatch: encoder delta emits ENCODER on the turned encoder", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(0, 0, +3, 0), /*map*/ false);
    REQUIRE(rec().enc_count == 1);
    REQUIRE(rec().enc_control == OC::CONTROL_ENCODER_L);
    REQUIRE(rec().enc_value == 3);

    rec() = Rec{};
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(0, 0, 0, -2), /*map*/ false);
    REQUIRE(rec().enc_control == OC::CONTROL_ENCODER_R);
    REQUIRE(rec().enc_value == -2);
}
```

Add the Makefile target after the `test-oc-router` block (mirrors it exactly):

```makefile
# Unit test for the consolidated customUI dispatch (oc_customui_dispatch.h).
build/host/test_oc_dispatch: harness/tests/test_oc_dispatch.cpp shim/src/oc/io.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-oc-dispatch
test-oc-dispatch: build/host/test_oc_dispatch
	./build/host/test_oc_dispatch
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test-oc-dispatch`
Expected: FAIL at compile, `oc_customui_dispatch.h: No such file or directory`.

- [ ] **Step 3: (no implementation in this task)**

Skip; Task 2 implements the header.

- [ ] **Step 4: Commit the test**

```bash
git add harness/tests/test_oc_dispatch.cpp Makefile
git commit -m "test(apps): failing dispatch_custom_ui unit test (#54)"
```

Note: if compile fails on a field name in `reinterpret_cast<const ::UI::Event&>`
(`.type` / `.control` / `.value`), open `vendor/O_C-Phazerville/software/src/UI/ui_events.h`
and match the actual member names; they are the same fields the per-app emit glue
constructs positionally today.

---

### Task 2: Implement `oc_customui_dispatch.h` (GREEN)

**Files:**
- Create: `plugins/apps/oc_customui_dispatch.h`

- [ ] **Step 1: Write the header**

Create `plugins/apps/oc_customui_dispatch.h`:

```cpp
#pragma once

// Consolidated O_C app customUI dispatch. Single source of truth for the
// transport glue every per-app .cpp used to copy: build a ::UI::Event from a
// firmware control gesture, bridge it to the vendor OC::UI::Event the app
// handler expects, and mirror any setting edit back into the NT parameter
// store. The runtime gesture state machine (held_since, classify_release, the
// mapping table, idle reset) lives in _per_app_runtime.h; this header composes
// it into the full customUI entry point.
//
// This is the ONLY file that pulls vendor UI/ui_events.h. Core
// _per_app_runtime.h and harness/include/oc_ui_sim.h stay UI-free so they avoid
// the OC::UI::Event vs ::UI::Event ambiguity under `using namespace OC`. Only
// per-app .cpp TUs (which already include ui_events.h) include this header.

#include "_per_app_runtime.h"
#include "UI/ui_events.h"

namespace oc_runtime {

// Construct a ::UI::Event and dispatch it to the app's button handler. The
// handler is typed void(*)(const OC::UI::Event&) (OC_apps.h forward-declares
// OC::UI::Event); ::UI::Event is layout-identical, so the reinterpret_cast
// bridge is the foundation's documented idiom.
inline void emit_button(AppAlgorithm& alg, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  last_controls_of(alg));
    alg.app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

inline void emit_encoder(AppAlgorithm& alg, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  last_controls_of(alg));
    alg.app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

// Mirror any app-side setting whose value diverged from the NT parameter store
// back into the store. Guarded by the construct-time sentinel (alg.alive) and a
// valid algorithm index, matching the runtime's parameterChanged guard.
// NT_setParameterFromUi indexes the GLOBAL parameter table, so the push target
// adds NT_parameterOffset(); alg.v is plug-in-relative, so the store compare
// uses base + s. Omitting the offset writes one global index low and the
// firmware re-applies the edit to the setting above the edited one.
inline void push_settings_to_params(AppAlgorithm& alg) {
    if (!alg.alive) return;
    const int32_t idx = NT_algorithmIndex(&alg);
    if (idx < 0) return;
    const int base = settings_param_base();
    const int n    = alg.settings_facade.num_settings;
    for (int s = 0; s < n; ++s) {
        const int v = alg.settings_facade.get_value(
            alg.settings_facade.instance, s);
        if (alg.v[base + s] != static_cast<int16_t>(v)) {
            NT_setParameterFromUi(static_cast<uint32_t>(idx),
                                  static_cast<uint32_t>(base + s) + NT_parameterOffset(),
                                  static_cast<int16_t>(v));
        }
    }
}

// Full customUI dispatch. Buttons emit on the release edge, classified short vs
// long by the runtime. map_long_press = true maps a long release to the vendor
// EVENT_BUTTON_LONG_PRESS the handler tests for (Harrington1200, FPART); false
// emits the raw classification (Low_rents, BBGEN, BYTEBEATGEN ignore
// LONG_RELEASE). Encoders emit one EVENT_ENCODER per non-zero delta. The push-
// back mirrors edits to the NT store. Runtime bookkeeping runs last so
// held_since / last_controls reflect the post-event state.
inline void dispatch_custom_ui(AppAlgorithm& alg, const _NT_uiData& data,
                               bool map_long_press) {
    if (!alg.app) return;

    int n = 0;
    const ControlMapping* tbl = button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;

    for (int i = 0; i < n; ++i) {
        const uint16_t bit  = tbl[i].nt_bit;
        const int      bi   = bit_index(bit);
        const bool now_down = (data.controls & bit) != 0;
        if ((edges & bit) && !now_down) {
            const uint8_t ev = classify_release(&alg, bi);
            const uint8_t out =
                map_long_press
                    ? (ev == EVENT_BUTTON_LONG_RELEASE ? EVENT_BUTTON_LONG_PRESS
                                                       : EVENT_BUTTON_PRESS)
                    : ev;
            emit_button(alg, tbl[i].oc_control, out);
        }
    }

    if (data.encoders[0] != 0) emit_encoder(alg, OC::CONTROL_ENCODER_L, data.encoders[0]);
    if (data.encoders[1] != 0) emit_encoder(alg, OC::CONTROL_ENCODER_R, data.encoders[1]);

    push_settings_to_params(alg);
    customUi(alg, data);
}

// Function-pointer factory matching _NT_factory.customUi. The map_long_press
// flag is a template parameter so each app gets a plain function pointer.
template <bool MapLongPress>
inline void dispatch_custom_ui_factory(_NT_algorithm* self,
                                       const _NT_uiData& data) {
    dispatch_custom_ui(*static_cast<AppAlgorithm*>(self), data, MapLongPress);
}

}  // namespace oc_runtime
```

- [ ] **Step 2: Run the test to verify it passes**

Run: `make test-oc-dispatch`
Expected: PASS (all 5 test cases).

- [ ] **Step 3: Verify the core runtime and harness stayed UI-free**

Run: `grep -n "ui_events" plugins/apps/_per_app_runtime.h harness/include/oc_ui_sim.h`
Expected: no output (neither file gained the include).

- [ ] **Step 4: Commit**

```bash
git add plugins/apps/oc_customui_dispatch.h
git commit -m "feat(apps): consolidated dispatch_custom_ui shared header (#54)"
```

---

### Task 3: Capture pre-migration `.text` baseline for the five apps

**Files:** none (records a reference artifact).

- [ ] **Step 1: Build the five app objects**

Run: `make build/arm/Low_rents.o build/arm/Harrington1200.o build/arm/FPART.o build/arm/BBGEN.o build/arm/BYTEBEATGEN.o`
Expected: five `.o` files under `build/arm/`.

- [ ] **Step 2: Record the `.text` hash of each**

Run:

```bash
for a in Low_rents Harrington1200 FPART BBGEN BYTEBEATGEN; do
  arm-none-eabi-objcopy -O binary --only-section=.text build/arm/$a.o /tmp/$a.text.before
  shasum -a 256 /tmp/$a.text.before
done | tee /tmp/oc_dispatch_text_before.txt
```

Expected: five sha256 lines saved. Keep `/tmp/*.text.before` for Task 9.

- [ ] **Step 3: (no commit; this is a measurement)**

---

### Task 4: Migrate `Low_rents.cpp` (map_long_press = false)

**Files:**
- Modify: `plugins/apps/Low_rents.cpp`
- Test: `make test-oc-app-Low_rents`

- [ ] **Step 1: Add the dispatch header include**

Near the existing `#include "_per_app_runtime.h"` (line 21), add below it:

```cpp
#include "oc_customui_dispatch.h"
```

- [ ] **Step 2: Delete the local glue block**

Delete the four local functions: `emit_button`, `emit_encoder`,
`push_settings_to_params`, `customUi_impl` (the block spanning roughly
`Low_rents.cpp:148-223`, from `void emit_button(...)` through the closing brace of
`customUi_impl`). Keep the comment header above it only if it still describes
remaining code; otherwise delete it too.

- [ ] **Step 3: Point the factory at the shared dispatch**

In the `_NT_factory factory` literal, change:

```cpp
    .customUi              = customUi_impl,
```

to:

```cpp
    .customUi              = oc_runtime::dispatch_custom_ui_factory<false>,
```

- [ ] **Step 4: Repoint the test seams to the runtime primitives**

In `low_rents_encoder_edit_freq1` and `low_rents_encoder_edit_setting`, replace the
deleted local calls:

```cpp
    emit_encoder(inst, OC::CONTROL_ENCODER_L, delta);
    push_settings_to_params(inst);
```

with:

```cpp
    oc_runtime::emit_encoder(*inst, OC::CONTROL_ENCODER_L, delta);
    oc_runtime::push_settings_to_params(*inst);
```

(and the `ENCODER_R` variant in `low_rents_encoder_edit_setting` likewise becomes
`oc_runtime::emit_encoder(*inst, OC::CONTROL_ENCODER_R, delta);`).

- [ ] **Step 5: Run the app's host test**

Run: `make test-oc-app-Low_rents`
Expected: PASS, same assertion count as before the change.

- [ ] **Step 6: Verify `.text` is unchanged**

Run:

```bash
make build/arm/Low_rents.o
arm-none-eabi-objcopy -O binary --only-section=.text build/arm/Low_rents.o /tmp/Low_rents.text.after
diff <(shasum -a 256 /tmp/Low_rents.text.before | cut -d' ' -f1) \
     <(shasum -a 256 /tmp/Low_rents.text.after  | cut -d' ' -f1) && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL`. If it differs, disassemble both
(`arm-none-eabi-objdump -d build/arm/Low_rents.o`) and confirm the delta is only
symbol/layout, not changed logic, before proceeding. Record the finding.

- [ ] **Step 7: Commit**

```bash
git add plugins/apps/Low_rents.cpp
git commit -m "refactor(apps): Low_rents adopts dispatch_custom_ui (#54)"
```

---

### Task 5: Migrate `Harrington1200.cpp` (map_long_press = true)

**Files:**
- Modify: `plugins/apps/Harrington1200.cpp`
- Test: `make test-oc-app-Harrington1200`

- [ ] **Step 1: Add the include**

Below `#include "_per_app_runtime.h"` (line 24), add `#include "oc_customui_dispatch.h"`.

- [ ] **Step 2: Delete the local glue block**

Delete `emit_button`, `emit_encoder`, `push_settings_to_params`, `customUi_impl`
(roughly `Harrington1200.cpp:162-243`). Harrington1200 has no extra test seams.

- [ ] **Step 3: Point the factory at the shared dispatch (TRUE)**

Change `.customUi = customUi_impl,` to
`.customUi = oc_runtime::dispatch_custom_ui_factory<true>,`.

- [ ] **Step 4: Run the app's host test**

Run: `make test-oc-app-Harrington1200`
Expected: PASS, unchanged assertion count.

- [ ] **Step 5: Verify `.text` unchanged**

```bash
make build/arm/Harrington1200.o
arm-none-eabi-objcopy -O binary --only-section=.text build/arm/Harrington1200.o /tmp/Harrington1200.text.after
diff <(shasum -a 256 /tmp/Harrington1200.text.before | cut -d' ' -f1) \
     <(shasum -a 256 /tmp/Harrington1200.text.after  | cut -d' ' -f1) && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL` (same disassembly fallback as Task 4 Step 6).

- [ ] **Step 6: Commit**

```bash
git add plugins/apps/Harrington1200.cpp
git commit -m "refactor(apps): Harrington1200 adopts dispatch_custom_ui (#54)"
```

---

### Task 6: Migrate `FPART.cpp` (map_long_press = true)

**Files:**
- Modify: `plugins/apps/FPART.cpp`
- Test: `make test-oc-app-FPART`

- [ ] **Step 1: Add the include**

Below `#include "_per_app_runtime.h"` (line 29), add `#include "oc_customui_dispatch.h"`.

- [ ] **Step 2: Delete the local glue block**

Delete `emit_button`, `emit_encoder`, `push_settings_to_params`, `customUi_impl`
(roughly `FPART.cpp:171-250`).

- [ ] **Step 3: Point the factory at the shared dispatch (TRUE)**

Change `.customUi = customUi_impl,` to
`.customUi = oc_runtime::dispatch_custom_ui_factory<true>,`.

- [ ] **Step 4: Repoint the FPART test seam**

FPART has a seam (around `FPART.cpp:327-328`) calling the deleted locals:

```cpp
    emit_encoder(inst, OC::CONTROL_ENCODER_R, delta);
    push_settings_to_params(inst);
```

becomes:

```cpp
    oc_runtime::emit_encoder(*inst, OC::CONTROL_ENCODER_R, delta);
    oc_runtime::push_settings_to_params(*inst);
```

- [ ] **Step 5: Run the app's host test**

Run: `make test-oc-app-FPART`
Expected: PASS, unchanged assertion count.

- [ ] **Step 6: Verify `.text` unchanged**

```bash
make build/arm/FPART.o
arm-none-eabi-objcopy -O binary --only-section=.text build/arm/FPART.o /tmp/FPART.text.after
diff <(shasum -a 256 /tmp/FPART.text.before | cut -d' ' -f1) \
     <(shasum -a 256 /tmp/FPART.text.after  | cut -d' ' -f1) && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL` (disassembly fallback as Task 4).

- [ ] **Step 7: Commit**

```bash
git add plugins/apps/FPART.cpp
git commit -m "refactor(apps): FPART adopts dispatch_custom_ui (#54)"
```

---

### Task 7: Migrate `BBGEN.cpp` (map_long_press = false)

**Files:**
- Modify: `plugins/apps/BBGEN.cpp`
- Test: `make test-oc-app-BBGEN`

- [ ] **Step 1: Add the include**

Below `#include "_per_app_runtime.h"` (line 12), add `#include "oc_customui_dispatch.h"`.

- [ ] **Step 2: Delete the local glue block**

Delete `emit_button`, `emit_encoder`, `push_settings_to_params`, `customUi_impl`
(roughly `BBGEN.cpp:141-192`). BBGEN has no extra test seams.

- [ ] **Step 3: Point the factory at the shared dispatch (FALSE)**

Change `.customUi = customUi_impl,` to
`.customUi = oc_runtime::dispatch_custom_ui_factory<false>,`.

- [ ] **Step 4: Run the app's host test**

Run: `make test-oc-app-BBGEN`
Expected: PASS, unchanged assertion count.

- [ ] **Step 5: Verify `.text` unchanged**

```bash
make build/arm/BBGEN.o
arm-none-eabi-objcopy -O binary --only-section=.text build/arm/BBGEN.o /tmp/BBGEN.text.after
diff <(shasum -a 256 /tmp/BBGEN.text.before | cut -d' ' -f1) \
     <(shasum -a 256 /tmp/BBGEN.text.after  | cut -d' ' -f1) && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL` (disassembly fallback as Task 4).

- [ ] **Step 6: Commit**

```bash
git add plugins/apps/BBGEN.cpp
git commit -m "refactor(apps): BBGEN adopts dispatch_custom_ui (#54)"
```

---

### Task 8: Migrate `BYTEBEATGEN.cpp` (map_long_press = false)

**Files:**
- Modify: `plugins/apps/BYTEBEATGEN.cpp`
- Test: `make test-oc-app-BYTEBEATGEN`

- [ ] **Step 1: Add the include**

Below `#include "_per_app_runtime.h"` (line 14), add `#include "oc_customui_dispatch.h"`.

- [ ] **Step 2: Delete the local glue block**

Delete `emit_button`, `emit_encoder`, `push_settings_to_params`, `customUi_impl`
(roughly `BYTEBEATGEN.cpp:145-196`). No extra test seams.

- [ ] **Step 3: Point the factory at the shared dispatch (FALSE)**

Change `.customUi = customUi_impl,` to
`.customUi = oc_runtime::dispatch_custom_ui_factory<false>,`.

- [ ] **Step 4: Run the app's host test**

Run: `make test-oc-app-BYTEBEATGEN`
Expected: PASS, unchanged assertion count.

- [ ] **Step 5: Verify `.text` unchanged**

```bash
make build/arm/BYTEBEATGEN.o
arm-none-eabi-objcopy -O binary --only-section=.text build/arm/BYTEBEATGEN.o /tmp/BYTEBEATGEN.text.after
diff <(shasum -a 256 /tmp/BYTEBEATGEN.text.before | cut -d' ' -f1) \
     <(shasum -a 256 /tmp/BYTEBEATGEN.text.after  | cut -d' ' -f1) && echo "TEXT IDENTICAL"
```

Expected: `TEXT IDENTICAL` (disassembly fallback as Task 4).

- [ ] **Step 6: Commit**

```bash
git add plugins/apps/BYTEBEATGEN.cpp
git commit -m "refactor(apps): BYTEBEATGEN adopts dispatch_custom_ui (#54)"
```

---

### Task 9: Full regression and ARM build

**Files:** none (verification + final state).

- [ ] **Step 1: Run the whole OC host test surface**

Run: `make test-oc-dispatch test-oc-router test-oc-runtime test-oc-apps test-oc-apps-all test-oc-io test-oc-menus test-oc-strings`
Expected: every binary PASS, zero failures.

- [ ] **Step 2: Build all ARM objects**

Run: `make arm`
Expected: clean build; each OC app `.o` well under the ~82 KB `.text` cap.

- [ ] **Step 3: Confirm all five `.text` sections matched baseline**

Run:

```bash
for a in Low_rents Harrington1200 FPART BBGEN BYTEBEATGEN; do
  arm-none-eabi-objcopy -O binary --only-section=.text build/arm/$a.o /tmp/$a.text.after
  if cmp -s /tmp/$a.text.before /tmp/$a.text.after; then echo "$a: IDENTICAL"; else echo "$a: DIFFERS"; fi
done
```

Expected: five `IDENTICAL` lines. Any `DIFFERS` must already have a recorded
disassembly justification from its migration task (logic unchanged, layout only).

- [ ] **Step 4: Confirm the duplication is gone**

Run: `grep -rn "void customUi_impl\|void emit_button\|void emit_encoder" plugins/apps/*.cpp`
Expected: no output (all five local copies deleted; only the shared header defines them).

- [ ] **Step 5: Final commit if anything outstanding, then open PR**

```bash
git push -u origin HEAD
```

Open the PR against the default branch (`main`) with a summary referencing #54 and
the byte-identical result, and a test plan covering the OC test surface and the
`.text` check. Note #63 (shipped-app refactor) is satisfied by this PR; leave it as
the closeout/verify tracker.

---

## Self-review

- Spec coverage: sibling-header decision (Task 2), pure-RF no-escape-hatch (Task 2 signature), all-five migration (Tasks 4-8), byte-identical gate (Tasks 3, 4-8 step, 9), map_long_press table (per-app factory bool in Tasks 4-8), test seams (Tasks 4, 6). Out-of-scope items (Hemisphere, StubApp) not touched. Covered.
- Placeholder scan: none; every code and command step is concrete.
- Type consistency: `dispatch_custom_ui(AppAlgorithm&, const _NT_uiData&, bool)`, `dispatch_custom_ui_factory<bool>`, `emit_encoder(AppAlgorithm&, uint16_t, int)`, `push_settings_to_params(AppAlgorithm&)` used identically in the header (Task 2), the test (Task 1), and the seams (Tasks 4, 6).
- Known risk: `.text` may not be byte-identical if `-O2` makes different inlining choices for header `inline` functions vs file-local statics. Each migration task carries the disassembly fallback to distinguish a layout-only delta (acceptable) from a logic change (a bug). If a `DIFFERS` is layout-only, record it and proceed.
