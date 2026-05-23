# Spec: Per-applet refactor pilot release

Date: 2026-05-19
Status: Active
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-19-per-applet-pilot-brainstorm.md`
Branch: `dr/per-applet-pilot`
Worktree: `.worktrees/per-applet-pilot`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Goal

Ship 6 per-applet pilot plug-ins plus 2 host plug-ins (`Hemispheres` 2-slot, `Quadrants` 4-slot) on `dr/per-applet-pilot`. Pilots: Compare, ClockDivider, VectorLFO, Cumulus, Relabi, ProbabilityDivider. Each per-applet `.o` ≤ 20 KB `.text`; each host `.o` ~16 KB. Hosts compose pilots at runtime via the `HemiPluginInterface` function-pointer-in-data ABI plus the firmware's `_NT_slot` API. Bundled `Hemispheres.o` and `Hemispheres2.o` retained. Manifest schema frozen at the post-integration checkpoint.

## Architecture

```
plugins/applets/<APPLET>.cpp           per-applet plug-in source
plugins/hosts/<HOST>.cpp               host plug-in source (Hemispheres, Quadrants)
plugins/probes/                        relocated diagnostic probes
plugins/applets/_per_applet_runtime.h  shared per-applet glue (no .cpp)
shim/include/HemiPluginInterface.h     versioned ABI struct
shim/include/applet_manifest.h         shared BusKind/BusParam types
shim/include/applet_manifests/<APP>.h  one per applet, declares guid/name/bus
shim/include/host_helpers.h            slot resolution, incompatible stub
shim/src/host_helpers.cpp              host helper impls
harness/tests/test_applet_<APP>.cpp    per-applet test file
harness/tests/test_host_<HOST>.cpp     per-host test file
```

Build artifacts after pilot integration:

| Artifact | Path | Approx text |
|---|---|---|
| Per-applet pilot (6) | `build/arm/<APPLET>.o` | 16-20 KB each |
| Hemispheres host | `build/arm/Hemispheres_host.o` | ~16 KB |
| Quadrants host | `build/arm/Quadrants_host.o` | ~16 KB |
| Bundled (retained) | `build/arm/Hemispheres.o`, `build/arm/Hemispheres2.o` | unchanged |

## HemiPluginInterface ABI

`shim/include/HemiPluginInterface.h`:

```cpp
#pragma once
#include <distingnt/api.h>
#include <cstdint>

constexpr uint32_t kHemiInterfaceMagic   = NT_MULTICHAR('H','M','I','1');
constexpr uint32_t kHemiInterfaceVersion = 1;

struct HemiPluginInterface : public _NT_algorithm {
    uint32_t magic;
    uint32_t interface_version;
    void (*render_view)(_NT_algorithm* self, int origin_x, int origin_y);
    void (*on_encoder_turn)(_NT_algorithm* self, int direction);
    void (*on_encoder_turn_shifted)(_NT_algorithm* self, int direction);
    void (*on_button_press)(_NT_algorithm* self);
    void (*on_aux_button)(_NT_algorithm* self);
};
```

Per-instance overhead above `_NT_algorithm` is 28 bytes: `magic` (4) + `interface_version` (4) + 5 function pointers (5 × 4). Accepted cost of versioning.

Per-applet `construct()` populates magic + version + all 5 function pointers. Default for shifted handler:

```cpp
inst->on_encoder_turn_shifted = inst->on_encoder_turn;
```

Hosts validate before calling through:

1. `NT_getSlot(slot, slot_idx)` (returns false → empty slot).
2. `(slot.guid() & 0xFFFF) == kHemiGuidPrefix` (rejects non-Hemi).
3. `HemiPluginInterface* p = static_cast<HemiPluginInterface*>(slot.plugin())` (null check).
4. `p->magic == kHemiInterfaceMagic` and `p->interface_version >= kHemiInterfaceVersion`.

Any failure renders the incompatible stub at the slot's origin and skips event routing for that slot.

## Guid prefix convention

Every Hemi-family plug-in uses the 2-char prefix `Hm` as the low two bytes of its NT 4CC guid. Example: ADEG → `NT_MULTICHAR('H','m','A','d')` → JSON `"HmAd"`.

`NT_MULTICHAR(a,b,c,d)` packs `a` at bits 0-7 (verified at `vendor/distingNT_API/include/distingnt/api.h:120`). The host filter:

```cpp
constexpr uint32_t kHemiGuidPrefix = NT_MULTICHAR('H','m',0,0) & 0xFFFF;

bool is_hemi_plugin(uint32_t guid) {
    return (guid & 0xFFFF) == kHemiGuidPrefix;
}
```

Each per-applet manifest carries a `static_assert((kAppletGuid & 0xFFFF) == kHemiGuidPrefix, ...)` enforcing the prefix.

## Per-applet manifest schema

`shim/include/applet_manifest.h`:

```cpp
#pragma once
#include <cstdint>

enum class BusKind : uint8_t {
    gate,
    cv,
    audio,
};

struct BusParam {
    const char* name;
    BusKind     kind;
};
```

`shim/include/applet_manifests/<APPLET>.h` (one per applet):

```cpp
#pragma once
#include "../applet_manifest.h"
#include "../HemiPluginInterface.h"
#include <distingnt/api.h>

namespace per_applet::<APPLET> {
    constexpr uint32_t      guid        = NT_MULTICHAR('H','m','X','x');
    constexpr const char*   name        = "<Display Name>";
    constexpr const char*   description = "<Short description>";
    constexpr BusParam      inputs[]    = { /* per applet */ };
    constexpr BusParam      outputs[]   = { /* per applet */ };

    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
}
```

The 6 pilot manifests:

| Pilot | Guid | Inputs | Outputs |
|---|---|---|---|
| Compare | `NT_MULTICHAR('H','m','C','p')` | `{"CV 1", cv}, {"CV 2", cv}` | `{"GT",  gate}, {"Min", cv}` |
| ClockDivider | `NT_MULTICHAR('H','m','C','d')` | `{"Clock", gate}, {"Reset", gate}` | `{"Out A", gate}, {"Out B", gate}` |
| VectorLFO | `NT_MULTICHAR('H','m','V','l')` | `{"Freq CV", cv}, {"Reset", gate}` | `{"Out A", cv}, {"Out B", cv}` |
| Cumulus | `NT_MULTICHAR('H','m','C','u')` | `{"Clock", gate}, {"CV", cv}` | `{"Out A", cv}, {"Out B", cv}` |
| Relabi | `NT_MULTICHAR('H','m','R','l')` | `{"Clock", gate}, {"Reset", gate}` | `{"Out A", cv}, {"Out B", cv}` |
| ProbabilityDivider | `NT_MULTICHAR('H','m','P','d')` | `{"Clock", gate}, {"Reset", gate}` | `{"Out A", gate}, {"Out B", gate}` |

Implementers verify the input/output names match the vendor applet's `OutputLabel(0)`/`OutputLabel(1)` and the gate-vs-cv kind matches what the applet writes to each output. Vendor inputs follow the standard Phazerville L/R/A/B layout; the manifest's `inputs` array binds to the applet's `In(0)` and `In(1)` calls plus `Clock(0)`/`Clock(1)` patterns.

## Per-applet `_per_applet_runtime.h` surface

Header-only helper at `plugins/applets/_per_applet_runtime.h`. All 6 pilot `.cpp` files include it and use its API verbatim. Implementers do NOT reinvent the glue.

```cpp
namespace per_applet_runtime {
    // Build the _NT_parameter[] table from a manifest's inputs/outputs arrays.
    // Applet-specific params follow the bus params.
    template <typename ManifestNS>
    constexpr int parameter_count();

    template <typename ManifestNS>
    void emit_parameters(_NT_parameter* dst);

    // Per-step bus-to-frame and frame-to-bus.
    template <typename ManifestNS>
    void populate_frame_from_bus(_NT_algorithm* self, float* busFrames, int numFramesBy4);

    template <typename ManifestNS>
    void write_outputs_to_bus(_NT_algorithm* self, float* busFrames, int numFramesBy4);

    // Inner-tick loop using the firmware's numFramesBy4 → ticks_this_step = numFrames / 3.
    // Matches the bundled host shim at shim/include/hemispheres_shim.h:179.
    template <typename Applet>
    void run_controller_inner_ticks(Applet* applet, int numFramesBy4);

    // Standalone customUi routing. Calls the HemiPluginInterface function pointers
    // populated by construct() — the same path the host uses.
    void route_custom_ui(_NT_algorithm* self, const _NT_uiData& data);

    // Serialise/deserialise wrappers around vendor OnDataRequest/OnDataReceive.
    template <typename Applet>
    void write_data_request(Applet* applet, _NT_jsonStream& stream);

    template <typename Applet>
    bool read_data_receive(Applet* applet, _NT_jsonParse& parse);
}
```

The 10x clocked-multiplier rule documented in `CLAUDE.md` lives entirely inside `run_controller_inner_ticks`. Per-applet `.cpp` files must not implement their own inner-tick loop.

## Per-applet plug-in template

Each `plugins/applets/<APPLET>.cpp`:

```cpp
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/<APPLET>.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/<APPLET>.h"
#include "../../shim/include/hemispheres_shim.h"
#include "_per_applet_runtime.h"

namespace { using ManifestNS = per_applet::<APPLET>; }

struct _AppletInstance : public HemiPluginInterface {
    <APPLET> applet;
};

// Render functions called via HemiPluginInterface pointers.
static void render_view_impl(_NT_algorithm* self, int origin_x, int origin_y) {
    auto* inst = static_cast<_AppletInstance*>(self);
    inst->applet.View();
}
static void on_encoder_turn_impl(_NT_algorithm* self, int dir) { /* applet.OnEncoderMove(dir) */ }
static void on_button_press_impl(_NT_algorithm* self)          { /* applet.OnButtonPress() */ }
static void on_aux_button_impl(_NT_algorithm* self)            { /* aux button — vendor-specific */ }

// _NT_factory hooks: calculateRequirements, construct, step, draw,
// parameterChanged, serialise, deserialise, hasCustomUi, customUi.

// hasCustomUi returns kNT_encoderL | kNT_encoderButtonL | kNT_button1 (standardized).
// customUi calls per_applet_runtime::route_custom_ui.
// draw calls inst->render_view(self, 0, 0); returns true.

_NT_factory factory = {
    .guid = ManifestNS::guid,
    .name = ManifestNS::name,
    .description = ManifestNS::description,
    .calculateRequirements = calculateRequirements_impl,
    .construct = construct_impl,
    .parameterChanged = parameterChanged_impl,
    .step = step_impl,
    .draw = draw_impl,
    .hasCustomUi = hasCustomUi_impl,
    .customUi = customUi_impl,
    .serialise = serialise_impl,
    .deserialise = deserialise_impl,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&factory : 0;
    }
    return 0;
}
```

### Per-applet hook contract

| Hook | Per-applet implementation |
|---|---|
| `calculateRequirements` | `req.numParameters = sizeof(inputs)/sizeof(BusParam) + sizeof(outputs)/sizeof(BusParam) + N_applet_params`. `req.sram = sizeof(_AppletInstance)`. Other pools 0. |
| `construct` | Placement-new `_AppletInstance`. Set `magic = kHemiInterfaceMagic`, `interface_version = kHemiInterfaceVersion`, populate the 5 function pointers (including `on_encoder_turn_shifted = on_encoder_turn` default). Call applet's `BaseStart()`. |
| `step` | Call `per_applet_runtime::populate_frame_from_bus<ManifestNS>(self, busFrames, numFramesBy4)`, then `run_controller_inner_ticks(&inst->applet, numFramesBy4)`, then `write_outputs_to_bus<ManifestNS>(self, busFrames, numFramesBy4)`. |
| `draw` | `((HemiPluginInterface*)self)->render_view(self, 0, 0); return true;`. |
| `parameterChanged` | Forward to applet parameter handler if it has one; otherwise no-op. |
| `serialise` | `per_applet_runtime::write_data_request(&inst->applet, stream);`. |
| `deserialise` | `return per_applet_runtime::read_data_receive(&inst->applet, parse);`. |
| `hasCustomUi` | Return `kNT_encoderL \| kNT_encoderButtonL \| kNT_button1`. Standardized. |
| `customUi` | Call `per_applet_runtime::route_custom_ui(self, data)`. Do NOT reimplement routing. |

## Host control claim model

### Hemispheres host (2 slots, 64×64 each)

```cpp
uint32_t hasCustomUi(_NT_algorithm*) {
    return kNT_encoderL | kNT_encoderR
         | kNT_encoderButtonL | kNT_encoderButtonR
         | kNT_button1 | kNT_button2;
}
```

| Control | Action |
|---|---|
| L encoder turn | Slot 0 `on_encoder_turn(direction)` |
| R encoder turn | Slot 1 `on_encoder_turn(direction)` |
| L encoder button edge | Slot 0 `on_button_press` |
| R encoder button edge | Slot 1 `on_button_press` |
| button1 edge | Slot 0 `on_aux_button` |
| button2 edge | Slot 1 `on_aux_button` |

The Hemispheres host renders two slot regions side-by-side: slot 0 at origin `(0, 0)`, slot 1 at origin `(64, 0)`. Each slot region is 64×64. If a slot fails validation, the host renders the incompatible stub at that origin.

### Quadrants host (4 slots, 64×64 each)

```cpp
uint32_t hasCustomUi(_NT_algorithm*) {
    return kNT_encoderL | kNT_encoderR
         | kNT_encoderButtonL | kNT_encoderButtonR
         | kNT_button1 | kNT_button2 | kNT_button3 | kNT_button4;
}
```

| Control | Action |
|---|---|
| button1 edge | Set focused slot = 0 |
| button2 edge | Set focused slot = 1 |
| button3 edge | Set focused slot = 2 |
| button4 edge | Set focused slot = 3 |
| L encoder turn | Focused slot `on_encoder_turn(direction)` |
| R encoder turn | Focused slot `on_encoder_turn_shifted(direction)` |
| L encoder button | Focused slot `on_button_press` |
| R encoder button | Focused slot `on_aux_button` |

Slot rendering: slot 0 at `(0, 0)`, slot 1 at `(64, 0)`, slot 2 at `(0, 32)`, slot 3 at `(64, 32)`. Each region 64×32 visually (Quadrants does 4 × half-height regions on the 128×64 screen).

The focused slot is drawn with an inverted border (1px). The focused-slot index is host state (stored on the host's algorithm instance), persisted via host serialise/deserialise.

The slot resolution result MUST be cached per draw cycle. `host_helpers.h`:

```cpp
struct ResolvedSlot {
    HemiPluginInterface* plugin;
    uint32_t             guid;
};
ResolvedSlot resolve_slot(uint32_t slot_idx);
```

Host calls `resolve_slot()` once per slot per draw or event; routes through the cached `plugin` if non-null. Repeated per-event re-queries during a single draw pass are forbidden.

## Incompatible stub

`shim/include/host_helpers.h` declares; `shim/src/host_helpers.cpp` defines:

```cpp
void render_incompatible_stub(int origin_x, int origin_y);
```

Draws a 64×64 outline (1-pixel border) starting at `(origin_x, origin_y)` with text `"INCOMPATIBLE"` centered horizontally at `y = origin_y + 30`. Same routine for Hemispheres and Quadrants. Quadrant focus border still draws around the stub when focused.

## Vendor dep accounting

Per-applet Makefile variables, audit-corrected:

```makefile
VENDOR_DEPS_Compare            :=
VENDOR_DEPS_ClockDivider       :=
VENDOR_DEPS_VectorLFO          :=
VENDOR_DEPS_Cumulus            :=
VENDOR_DEPS_Relabi             :=
VENDOR_DEPS_ProbabilityDivider :=
```

All 6 pilots have empty vendor `.cpp` deps. The kickoff's lorenz assignment for VectorLFO and Relabi is rejected by the audit (no lorenz path through either include graph). Each per-applet manifest header records its dep list as a comment for human review:

```cpp
// Vendor deps: (none)
namespace per_applet::Compare { ... }
```

## Per-applet entries

### Compare

- Vendor file: `applets/Compare.h` (no internal `#include`s).
- Deps: none.
- Standalone solo-probe: 0 B unique.
- Manifest: see table above.
- Test concerns: round-trip is trivial (vendor `OnDataRequest` returns 0); test confirms `OnDataRequest() == 0` directly. Behavior tests cover comparator output through standalone bus I/O. No 10x ticks-per-step concern (no clock-driven state).

### ClockDivider

- Vendor file: `applets/ClockDivider.h`. Includes `../util/clkdivmult.h` (in shim baseline).
- Deps: none.
- Standalone solo-probe: not previously measured. Integration step records.
- Manifest: see table above.
- Test concerns: round-trip via existing `pack_clockdivider` pattern (bias `+32` on `div[i]`, AND with field-width mask). Clock-driven state evolution; relies on `_per_applet_runtime::run_controller_inner_ticks` to fire vendor `Controller()` 10× per buffer. Bus-level fire-count assertions follow the 10x rule per `CLAUDE.md`.

### VectorLFO

- Vendor file: `applets/VectorLFO.h`. Includes `vector_osc/HSVectorOscillator.h`, `vector_osc/WaveformManager.h`, `tideslite.h`. All in shim baseline. `tideslite.cpp` not linked (`constexpr ComputePhaseIncrement` only).
- Deps: none.
- Standalone solo-probe: not previously measured.
- Manifest: see table above.
- Test concerns: vec-osc state evolution observable through output bus. Round-trip via `pack_vectorlfo`. Confirms vendor dep accounting works on the per-`.o` partial-link path.

### Cumulus

- Vendor file: `applets/Cumulus.h` (no internal `#include`s).
- Deps: none.
- Standalone solo-probe: not previously measured.
- Manifest: see table above.
- Test concerns: this is the canonical 10x ticks-per-step coverage. Cumulus advances counters inside `if (Clock(ch))`. The test must either (a) model the 10x multiplier explicitly in fire-count assertions (per Cumulus CU2 in the bundled tests at `harness/tests/test_hemispheres.cpp:1264`), or (b) drop bus-level fire-count assertions and cover via round-trip + state injection only. The implementer MUST acknowledge this in the test file's header comment.

### Relabi

- Vendor file: `applets/Relabi.h`. Includes `../HSRelabiManager.h` (vendor-located, header-only), `vector_osc/HSVectorOscillator.h`, `vector_osc/WaveformManager.h`. All accessible.
- Deps: none.
- Standalone solo-probe: 4204 B unique. Largest pilot.
- Manifest: see table above.
- Test concerns: RelabiManager state visible through output bus. Round-trip via `pack_relabi`. SegmentDisplay text rendering surfaces during `View()`; covered via render snapshot if available, otherwise observed manually at hardware smoke.

### ProbabilityDivider

- Vendor file: `applets/ProbabilityDivider.h`. Includes `../HSProbLoopLinker.h` (vendor-located, header-only).
- Deps: none.
- Standalone solo-probe: not previously measured.
- Manifest: see table above.
- Test concerns: ProbLoopLinker is a singleton (`HSProbLoopLinker::get()` returns one instance). In the per-applet plug-in shape, that singleton is private to ProbabilityDivider's `.o` — different `.o` files cannot share the singleton. The test confirms ProbLoopLinker state survives round-trip within a single per-applet `.o` instance. If ProbabilityMelody is ever ported as a separate `.o`, the two singletons will be independent (gotcha documented in the manifest header comment).

## Host entries

### Hemispheres host

- File: `plugins/hosts/Hemispheres.cpp`.
- Guid: `NT_MULTICHAR('H','m','H','h')` (renders `"HmHh"` in JSON).
- Name: `"Hemispheres Host"`. Description: `"Composes 2 Hemi applets in slots."`
- State: focused-slot is implicit (button1 = slot 0, button2 = slot 1 by control map; no separate focus state).
- Algorithm instance state: 2 × `uint32_t` slot indices (the slot the host watches; configurable via parameter or auto-discovered).
- `hasCustomUi`: see table above.
- `customUi`: dispatches per the control-map table.
- `draw`: for each slot, resolve via `host_helpers::resolve_slot`, then either `plugin->render_view(plugin, slot_origin_x, 0)` or `render_incompatible_stub(slot_origin_x, 0)`.
- `step`: pass-through (host does not own audio; per-applet step is responsible for bus I/O).
- Test concerns: see test strategy below.

### Quadrants host

- File: `plugins/hosts/Quadrants.cpp`.
- Guid: `NT_MULTICHAR('H','m','Q','q')` (renders `"HmQq"` in JSON).
- Name: `"Quadrants Host"`. Description: `"Composes 4 Hemi applets with focused-slot control."`
- State: 4 × `uint32_t` slot indices + 1 × `uint8_t` focused-slot index (0-3).
- `hasCustomUi`: see table above.
- `customUi`: button1-4 set focused slot; L/R encoder + buttons route to focused slot per the control map.
- `draw`: for each slot, resolve and render at the 2×2 grid position. Focused slot gets inverted 1px border.
- `step`: pass-through.
- Serialise/deserialise: persist the 4 slot indices and the focused-slot index.
- Test concerns: see test strategy below.

## Test strategy

### Per-applet tests (`harness/tests/test_applet_<APP>.cpp`)

Each pilot ships one Catch2 test file. Setup commit creates a skeleton (see "Setup commit file inventory" below); each per-applet implementer fills in the test.

Coverage required per pilot:

- Round-trip: pack/unpack via the applet's existing `pack_<applet>` helper if any, or assert `OnDataRequest() == 0` if the vendor returns no state.
- Behavior: drive known inputs through standalone bus paths and assert observable outputs match expected.
- `customUi` standalone: drive a `_NT_uiData` with `encoders[0] = 1` and confirm the applet's encoder counter (where it has one) advances. Drive a `kNT_encoderButtonL` edge and confirm `OnButtonPress()` fires. Drive a `kNT_button1` edge and confirm the aux-button handler fires.
- Cumulus specifically: acknowledge 10x ticks-per-step in test file header comment; either model it explicitly or use round-trip + state injection only.
- ProbabilityDivider specifically: confirm ProbLoopLinker singleton state survives round-trip within the same per-applet `.o` instance.

### Host tests (`harness/tests/test_host_<HOST>.cpp`)

Each host ships one Catch2 test file. Required coverage:

- Happy path: install a stub `HemiPluginInterface` with valid magic + version into slots 0..N-1; route encoder + button events; assert each event reaches the expected slot's stub handler.
- ABI-mismatch test: install a stub with `magic = 0`; assert host renders incompatible stub at that slot's origin and skips event routing.
- Wrong-guid test: install an algorithm with a non-`'Hm'` guid prefix; assert guid filter rejects and stub renders.
- Empty-slot test: leave a slot index unconfigured; assert `NT_getSlot` returns false → stub rendered, no crash.
- Quadrants specifically: confirm `button1-4` edges set focused-slot index correctly; L/R encoder routes only to focused slot.

### Integration table (publish in PR body)

- `arm-none-eabi-size` `.text` for each of the 6 pilots plus the 2 hosts.
- `.text` for bundled `Hemispheres.o` plus `Hemispheres2.o` (confirms no regression).
- ITC consumption per pilot from `Misc > Plug-ins > View Info` on hardware.

## Setup commit file inventory

Authored by parent agent on `dr/per-applet-pilot` BEFORE implementer fan-out. Files:

- `shim/include/HemiPluginInterface.h` — versioned ABI struct.
- `shim/include/applet_manifest.h` — shared `BusKind`/`BusParam` types.
- `shim/include/host_helpers.h` — `ResolvedSlot`, `resolve_slot()`, `render_incompatible_stub()` declarations.
- `shim/src/host_helpers.cpp` — slot validation, guid filter, stub renderer implementations.
- `plugins/applets/_per_applet_runtime.h` — template helpers for parameter assembly, bus I/O, inner-tick loop, customUi routing, serialise/deserialise wrappers.
- `plugins/applets/.gitkeep`, `plugins/hosts/.gitkeep`, `plugins/probes/.gitkeep` — directory anchors.
- `Makefile` updates: per-applet rule macro, host rule macro, `PILOT_APPLET_LIST := Compare ClockDivider VectorLFO Cumulus Relabi ProbabilityDivider`, host build targets, probe path migrations.
- `.git/hooks/pre-commit` updates: accept `dr/per-applet-pilot`, `per-applet-applet/*`, `per-applet-host/*` branches; enforce per-implementer allowed surface and hard-reject the setup-owned triad (`HemiPluginInterface.h`, `applet_manifest.h`, `host_helpers.{h,cpp}`, `_per_applet_runtime.h`, `test_hemispheres.cpp`).
- `harness/tests/test_applet_<applet>.cpp` skeletons (6 files; one per pilot) — each contains a header comment, single placeholder `TEST_CASE`, and a list of per-applet test concerns.
- CLAUDE.md addition: `applets/` directory deprecation note (existing host files retained until cleanup release).

## Spec footer

### Recipe spot-check

- HemiPluginInterface size: 5 function pointers × 4 bytes + 2 × `uint32_t` = 28 bytes overhead above `_NT_algorithm`. Confirmed in ABI section.
- Per-applet `hasCustomUi` returns 3 bits: `kNT_encoderL` (bit 9), `kNT_encoderButtonL` (bit 7), `kNT_button1` (bit 0). Bitmask = `0x281`. Confirmed against `vendor/distingNT_API/include/distingnt/api.h:342-357`.
- Hemispheres host claim bitmask: `kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR | kNT_button1 | kNT_button2 = 0x683`. Quadrants adds `kNT_button3 | kNT_button4 = 0x68F`.
- `ticks_this_step = numFrames / 3` (verified at `shim/include/hemispheres_shim.h:179`). `_per_applet_runtime::run_controller_inner_ticks` uses the same formula; pilots inherit the 10x rate.

### Per-entry verification

- Compare: 0 includes confirmed via `grep '#include' vendor/.../applets/Compare.h` (empty output). Empty deps correct.
- ClockDivider: 1 include `../util/clkdivmult.h` confirmed at `vendor/.../applets/ClockDivider.h:21`. Header in baseline. Empty deps correct.
- VectorLFO: 3 includes at `vendor/.../applets/VectorLFO.h:21-23`. All in baseline. No lorenz path. Empty deps correct.
- Cumulus: 0 includes. Empty deps correct.
- Relabi: 3 includes at `vendor/.../applets/Relabi.h:27-29`. All resolvable. No lorenz path. Empty deps correct.
- ProbabilityDivider: 1 include `../HSProbLoopLinker.h` at `vendor/.../applets/ProbabilityDivider.h:21`. Header-only, vendor-located, resolves via `-Ivendor/.../applets`. Empty deps correct.

### Shim prereq verification

- `shim/include/hemispheres_shim.h:179` defines `ticks_this_step = numFrames / 3`. `_per_applet_runtime` mirrors this.
- `shim/include/vector_osc/` re-exports `HSVectorOscillator.h`, `WaveformManager.h`, `waveform_library.h`. VectorLFO and Relabi compile against the existing shim path.
- `shim/include/tideslite/tideslite.h` re-exports vendor `tideslite.h`. VectorLFO calls only `constexpr ComputePhaseIncrement`.
- `shim/include/util/clkdivmult.h` exists. ClockDivider compiles.
- `HSRelabiManager.h` and `HSProbLoopLinker.h` are vendor-located but accessible via the `-Ivendor/.../applets` path + `..` parent reference. No shim re-export needed.
- The current pre-commit hook in `.git/hooks/pre-commit` is Phase 6/Phase 5 vintage; setup commit updates it before implementer fan-out.

### Deviation footer

- Hardware `hasCustomUi` probe deferred to step 10 hardware smoke. Justification in brainstorm §"Deviation from kickoff step 1.6". The deferral does not weaken any abort condition; ABORT-A1 is cleared from header inspection, and step 10's standalone-deploy of each pilot tests firmware-honors-the-claim behavior directly.
- `VENDOR_DEPS_VectorLFO` and `VENDOR_DEPS_Relabi` set to empty, contradicting kickoff §"Vendor dep accounting". Audit found no lorenz path through either include graph. Adopted per audit.
