# Design: consolidate O_C app customUI dispatch into a shared header

Date: 2026-05-30
Status: approved
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`
Issue: #54 (mechanism + migration), #63 (closeout/verify), foundation #29

## Context

The O_C per-app control-gesture dispatch is centralized in logic but duplicated
in transport. The runtime (`plugins/apps/_per_app_runtime.h`) owns the gesture
state machine (`held_since`, edge detection, `classify_release`, the long-press
threshold, idle/screensaver reset, the `kNT_*` to `OC::CONTROL_*` mapping table).
Each per-app `.cpp` re-implements the dispatch glue around it: `emit_button`,
`emit_encoder`, `push_settings_to_params`, and `customUi_impl`, roughly 80 lines
per app, copy-pasted across the five shipped ports. The copied region includes the
two bits that must never drift: the on-device `NT_parameterOffset()` push-back
(a documented off-by-one) and the long-press mapping. The #36 backlog (#44-#52)
would add nine more copies.

This consolidates the glue into one shared header so each per-app `customUi`
collapses to a single line, and the backlog inherits a canonical dispatch instead
of fresh copies.

## Decisions

- **Single source of truth in a NEW sibling header** -
  `plugins/apps/oc_customui_dispatch.h`, namespace `oc_runtime`. It includes both
  `_per_app_runtime.h` and vendor `UI/ui_events.h`. NOT folded into core
  `_per_app_runtime.h`.
- **Pure runtime-fields, no escape hatch** - the dispatch owns event construction,
  the edge loop, short/long classification, and the push-back. Per-app variation is
  one boolean (`map_long_press`). No `emit_override` function pointer; the #54
  gesture-vocabulary enumeration proves no in-scope `OC::App` needs a bespoke path.
- **All five shipped apps migrate in the #54 PR** - Low_rents, Harrington1200,
  FPART, BBGEN, BYTEBEATGEN. #63 becomes closeout/verify, not the migration vehicle.
- **Byte-identical `.text` is the correctness gate** - this is pure transport
  de-duplication. The emitted ARM code per app must not change.

### Why a sibling header, not core `_per_app_runtime.h`

Issue #54 argues that pulling vendor `UI/ui_events.h` into the core runtime header
is free, citing `harness/include/oc_ui_sim.h` as already co-including both
`ui_events.h` and `_per_app_runtime.h`. That premise is inaccurate. The file was
checked directly: `harness/include/oc_ui_sim.h:30-35` includes `<distingnt/api.h>`,
`<cstdint>`, `<initializer_list>`, `OC_core.h`, and `_per_app_runtime.h`. It does
NOT include `ui_events.h`. It drives only the runtime bookkeeping
`oc_runtime::customUi(alg, data)` and never constructs a `UI::Event`.

Core `_per_app_runtime.h` deliberately stays decoupled from vendor UI to dodge the
`OC::UI::Event` (forward-declared in `OC_apps.h`) versus top-level `::UI::Event`
(defined in `ui_events.h`) ambiguity that arises under `using namespace OC`. The
runtime header never does `using namespace OC`; the per-app TUs do. Two current
consumers of core `_per_app_runtime.h` are UI-free today and should stay so:

- `harness/include/oc_ui_sim.h` (host input driver).
- The core runtime's own bookkeeping path.

Confining the `ui_events.h` include to the new `oc_customui_dispatch.h` keeps the
vendor-UI coupling in the single file that builds events. Only the five per-app
`.cpp` TUs (which already `#include "UI/ui_events.h"`) include the new header. The
core runtime and the harness sim are untouched. Blast radius: the five app TUs and
nothing else.

## Architecture

Three layers, only the middle one changes:

1. Firmware to runtime (transport in): firmware hands `customUi` a raw
   `_NT_uiData`. Unchanged.
2. Dispatch (gesture to event): edge loop, short/long classification, event
   construction, push-back. THIS moves from each per-app `.cpp` into the shared
   header.
3. Vendor handler (semantics): `HandleButtonEvent` / `HandleEncoderEvent` switch on
   `(type, control)`. Already per-app, untouched.

### New file: `plugins/apps/oc_customui_dispatch.h`

```cpp
#pragma once
#include "_per_app_runtime.h"
#include "UI/ui_events.h"   // ::UI::Event; confined to this file

namespace oc_runtime {

// Build a ::UI::Event and bridge it to the vendor OC::UI::Event the app handler
// expects (layout-identical; OC_apps.h only forward-declares OC::UI::Event).
void emit_button(AppAlgorithm& alg, uint16_t oc_control, uint8_t ev_type);
void emit_encoder(AppAlgorithm& alg, uint16_t oc_control, int delta);

// Mirror any app-side setting edit back into the NT parameter store. Adds
// NT_parameterOffset() to the global push index (the documented off-by-one).
// Type-erased through alg.settings_facade, so quad apps work unchanged.
void push_settings_to_params(AppAlgorithm& alg);

// Full customUI dispatch: button release edges (classified short/long),
// encoder deltas, then push-back. map_long_press = true maps a LONG_RELEASE
// classification to the vendor EVENT_BUTTON_LONG_PRESS; false emits the raw
// classification (apps with no long-press path ignore LONG_RELEASE).
void dispatch_custom_ui(AppAlgorithm& alg, const _NT_uiData& data,
                        bool map_long_press);

// One-line factory matching the _NT_factory.customUi signature. The bool is a
// template parameter so each app gets a plain function pointer.
template <bool MapLongPress>
inline void dispatch_custom_ui_factory(_NT_algorithm* self,
                                       const _NT_uiData& data) {
    dispatch_custom_ui(*static_cast<AppAlgorithm*>(self), data, MapLongPress);
}

}  // namespace oc_runtime
```

All four functions take `AppAlgorithm&` (the base type). Every member they touch is
on the base: `app`, `last_controls`, `v`, `alive`, `settings_facade`, and
`NT_algorithmIndex(...)`. No derived-type access. The quad apps (BBGEN,
BYTEBEATGEN) reach their channels through `settings_facade`, which is already
type-erased, so they need no special handling.

`dispatch_custom_ui` is the exact union of the two current `customUi_impl`
variants. Its body, distilled from `Low_rents.cpp:189` and `Harrington1200.cpp`:

```cpp
void dispatch_custom_ui(AppAlgorithm& alg, const _NT_uiData& data,
                        bool map_long_press) {
    if (!alg.app) return;
    int n = 0;
    const ControlMapping* tbl = button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;
    for (int i = 0; i < n; ++i) {
        const uint16_t bit = tbl[i].nt_bit;
        const int bi = bit_index(bit);
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
    customUi(alg, data);   // runtime bookkeeping, AFTER emit (post-event state)
}
```

Note the `map_long_press == false` branch emits `ev` raw, exactly reproducing
Low_rents/BBGEN/BYTEBEATGEN today (which call `emit_button(inst, control, classify_release(...))`
with no remap).

### Per-app `.cpp` collapse

Each app:

- adds `#include "oc_customui_dispatch.h"` (can drop its own `ui_events.h` include
  since the new header pulls it; keep it if the namespace-alias dance below needs
  it ordered first, decided per-app at build time).
- deletes its local `emit_button`, `emit_encoder`, `push_settings_to_params`,
  `customUi_impl` (about 80 lines).
- sets the factory field:

```cpp
.customUi = oc_runtime::dispatch_custom_ui_factory<false>,  // Low_rents, BBGEN, BYTEBEATGEN
.customUi = oc_runtime::dispatch_custom_ui_factory<true>,   // Harrington1200, FPART
```

- rewrites its test seams to call the runtime primitives. Low_rents
  (`low_rents_encoder_edit_freq1`, `low_rents_encoder_edit_setting`) and FPART each
  call the deleted locals; they become
  `oc_runtime::emit_encoder(*inst, ...)` and `oc_runtime::push_settings_to_params(*inst)`.

### Per-app `map_long_press` table

| App | File | `map_long_press` | Reason |
| --- | --- | --- | --- |
| Low_rents (LORENZ) | `plugins/apps/Low_rents.cpp` | false | No long-press path; handler ignores LONG_RELEASE |
| Harrington1200 (H1200) | `plugins/apps/Harrington1200.cpp` | true | Reads `EVENT_BUTTON_LONG_PRESS` on `CONTROL_BUTTON_L` |
| FPART | `plugins/apps/FPART.cpp` | true | Long-press on DOWN (page) and L (copy/paste) |
| BBGEN | `plugins/apps/BBGEN.cpp` | false | Emits `classify_release` raw; no long-press |
| BYTEBEATGEN | `plugins/apps/BYTEBEATGEN.cpp` | false | Emits `classify_release` raw; no long-press |

## Error handling

- `dispatch_custom_ui` early-returns on `!alg.app` (construct-time / unarmed),
  matching the current per-app guard.
- `push_settings_to_params` keeps both existing guards: `!alg.alive` and
  `NT_algorithmIndex(...) < 0`.
- No new failure modes. The reinterpret_cast bridge from `::UI::Event` to
  `OC::UI::Event` is the existing one, moved verbatim.

## Testing

TDD order:

1. Extend `test_oc_router.cpp` (or a new `test_oc_dispatch.cpp`) to drive
   `dispatch_custom_ui` directly against a stub `AppAlgorithm` and assert: a short
   release emits PRESS; a long release with `map_long_press=false` emits
   LONG_RELEASE; the same with `map_long_press=true` emits LONG_PRESS; an encoder
   delta emits ENCODER on the correct control; push-back fires only when a setting
   diverged and uses the offset. Write these RED before the header exists.
2. Implement `oc_customui_dispatch.h` to green.
3. Migrate the five apps one at a time, keeping `test-oc-app-<APP>` green after each.

Regression gates (must stay green): `test-oc-runtime`, `test-oc-router`,
`test-oc-apps`, `test-oc-apps-all` (every `test-oc-app-<APP>`), `test-oc-io`,
`test-oc-menus`, `test-oc-strings`.

Correctness gate (as-built result): `.text` is NOT byte-identical. The
section-merge step (`shim/merge_sections.lds`) relocates the now-shared inline
functions, so every `.text` address shifts and the raw bytes differ. The
achievable guarantee is instruction-level identity: for each app, the mnemonic
histogram (`objdump -d` counts per opcode) of the migrated `.o` equals the
pre-migration `.o` exactly, and the per-app host suite passes through the real
dispatch path. All five shipped apps plus StubApp verified HISTOGRAM IDENTICAL
with green host tests (Low_rents 176, Harrington1200 196, FPART 87, BBGEN 241,
BYTEBEATGEN 942, StubApp 32 assertions). The earlier "byte-identical" framing was
too strict; logic is unchanged, only code layout moved.

## Out of scope

- The Hemisphere side (`_per_applet_runtime.h::route_custom_ui`, the composer host
  `customUi_impl`). Same gesture vocabulary, separate domain. Unifying OC and
  Hemisphere is a later, larger question.
- Any new gesture type, `event.mask` (button-combo) handling, `BUTTON_UP2/DOWN2`.
  The #54 enumeration shows zero in-scope app reads them.
- `StubApp.cpp` migration unless it carries the glue (verify during execution).

## Spec footer

### Recipe spot-check

The canonical recipe is the new header plus the per-app collapse. Spot-checked
against `Low_rents.cpp:148-223` (the `false` variant, with two test seams) and
`Harrington1200.cpp` `customUi_impl` (the `true` variant, the long-press remap).
The distilled `dispatch_custom_ui` body above is the literal union; the only
runtime-data difference between the two source variants is the `map_long_press`
branch, confirmed by diffing the two `customUi_impl` functions (identical outside
the remap line and the instance-type cast).

### Per-entry verification (3 of 5 apps traced end-to-end against source)

- Low_rents (`false`): `Low_rents.cpp:203-205` emits `classify_release(inst, bi)`
  raw with no remap. Matches the `map_long_press=false` branch. Seams at
  `:276-294` call `emit_encoder` + `push_settings_to_params`. CONFIRMED.
- Harrington1200 (`true`): `customUi_impl` maps `EVENT_BUTTON_LONG_RELEASE` to
  `EVENT_BUTTON_LONG_PRESS`, else `EVENT_BUTTON_PRESS`. Matches the
  `map_long_press=true` branch. No extra seams. CONFIRMED.
- BBGEN (`false`): `BBGEN.cpp:183` emits `classify_release(inst, bi)` raw, no
  remap, quad facade through `settings_facade`. Matches `false`. CONFIRMED.

3 of 3 traced agree with the table. No contradictions found; full audit not
triggered.

### Shim prereq verification

No shim additions. The consolidation reuses existing runtime symbols
(`button_mapping_table`, `classify_release`, `bit_index`, `customUi`,
`settings_param_base`, `EVENT_*`, `ControlMapping`) and the existing
`::UI::Event` / `OC::UI::Event` bridge. The only new file is
`plugins/apps/oc_customui_dispatch.h`. No Makefile target changes (the apps already
build via `BUILD_PER_OC_APP`; the new header is header-only and pulled by each app
TU). `oc_ui_sim.h` and core `_per_app_runtime.h` are not edited.
