# Per-applet mass-port design

Date: 2026-05-20
Owner: Dan
Prior spec: `docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Context

The pilot release proved the per-applet template and the
HemiPluginInterface ABI on hardware. Mass-port carries the pilot
lessons forward to the remaining 49 applets. This spec encodes the
corrections that apply to every implementer prompt.

## Canonical recipe

Each per-applet port produces three files. The plug-in lives at
`plugins/applets/<APPLET>.cpp`, the manifest at
`shim/include/applet_manifests/<APPLET>.h`, and the test at
`harness/tests/test_applet_<APPLET>.cpp`. The vendor source under
`vendor/O_C-Phazerville/software/src/applets/<APPLET>.h` is untouched.

### Manifest

Manifest is a `struct` inside `namespace per_applet`, not a nested
namespace. The runtime helpers take a TYPE parameter
(`typename ManifestNS`) and cannot consume a namespace.

```cpp
namespace per_applet {
struct <APPLET> {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','X','x');
    static constexpr const char*   name        = "...";
    static constexpr const char*   description = "...";
    static constexpr BusParam      inputs[]    = { ... };
    static constexpr BusParam      outputs[]   = { ... };
    static_assert((guid & 0xFFFF) == kHemiGuidPrefix,
                  "Hemi applet guid must start with 'Hm'");
};
}
```

### Per-applet plug-in

Canonical include order. Do NOT include `hemispheres_shim.h`; it pulls
in `HemispheresFactory.h` which includes all 56 vendor applet headers
and triggers an ODR collision against the local applet TU.

```cpp
#include "../../shim/include/HemisphereApplet.h"
#include "../../shim/include/PhzIcons.h"
#include "../../shim/include/Arduino.h"
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/<APPLET>.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/<APPLET>.h"
#include "_per_applet_runtime.h"
```

`_AppletInstance` carries the vendor applet plus a
`per_applet_runtime::PerInstanceState input_state` field for
per-instance changed-cv and rising-edge tracking. When the manifest
struct name collides with the vendor class name, alias one:

```cpp
namespace { using ManifestNS = per_applet::<APPLET>; }
struct _AppletInstance : public HemiPluginInterface {
    ::<APPLET> applet;                                 // global vendor class
    per_applet_runtime::PerInstanceState input_state;
};
```

`construct_impl` wires the HemiPluginInterface function pointers,
including:

```cpp
inst->render_view = &per_applet_runtime::render_view_with_offset<_AppletInstance>;
```

The runtime helper sets both `HS::gfx_offset = origin_x` and
`HS::gfx_offset_y = origin_y` before calling the vendor `View()`, then
clears both back to 0. Do NOT write a local `render_view_impl`.

`step_impl` calls into the runtime helpers, passing the per-instance
state by reference:

```cpp
static void step_impl(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* inst = static_cast<_AppletInstance*>(self);
    per_applet_runtime::populate_frame_from_bus<ManifestNS>(self, busFrames, numFramesBy4, inst->input_state);
    per_applet_runtime::run_controller_inner_ticks(&inst->applet, numFramesBy4);
    per_applet_runtime::write_outputs_to_bus<ManifestNS>(self, busFrames, numFramesBy4);
}
```

### Test seam

Default: `extern "C"` opaque accessors. The per-applet test file
cannot include the vendor header (ODR collision against the per-applet
TU). Define accessors in the per-applet .cpp so they cast inside the
TU where `_AppletInstance` is fully visible:

```cpp
extern "C" uint64_t <applet>_on_data_request(_NT_algorithm* self);
extern "C" void     <applet>_on_data_receive(_NT_algorithm* self, uint64_t state);
```

Inline `pack_<applet>` helpers in the test file are allowed only when
the test specifically validates the pack format (byte layout,
bit-packing edge cases). Default to opaque accessors. Implementer
prompts that deviate must justify it in the commit message.

### Output mode parameter naming

The runtime now emits `"<output name> mode"` for each output's mode
parameter rather than a generic `"Output mode"`. Manifests do not
need to declare mode names explicitly; the runtime builds them from
`outputs[i].name`.

## Pilot lessons (carried throughout)

1. Manifest is a struct in `namespace per_applet`, not a nested namespace.
2. `render_view_with_offset<_AppletInstance>` is the only acceptable
   render_view wiring. Local impls miss the gfx_offset_y handling and
   break host rendering in Quadrants.
3. Canonical include order. Including `hemispheres_shim.h` is
   forbidden.
4. Alias vendor class with `::<APPLET>` when the manifest struct name
   collides.
5. Opaque `extern "C"` accessors are the default test seam.
6. Per-applet test files are NOT linked against
   `applet_test_helpers.cpp`. They link against the per-applet `.cpp`
   only.
7. Local `_NT_slot::*` stubs in `test_host_*.cpp` are forbidden. The
   central stubs in `harness/src/nt_runtime.cpp` cover every host test
   binary.

## Per-applet entries

Categorization from the pre-Layer-0 audit (Gate B). All 49 applets
ready to fan out. Test coverage shape comes from Gate C for Batch 4
applets; other batches default to the standard pilot pattern.

### Batch 1a: trivial canary (5)

| Applet | Vendor deps | Test shape | Notes |
| --- | --- | --- | --- |
| AttenuateOffset | none | standard | |
| Binary | `SegmentDisplay.h` (header-only) | standard | |
| Button | none | standard | `OnDataRequest()` returns 0; assert that directly |
| Logic | none | standard | |
| Switch | none | standard | `OnDataRequest()` returns 0 |

### Batch 1b: trivial continued (7)

| Applet | Vendor deps | Test shape | Notes |
| --- | --- | --- | --- |
| Brancher | none | standard | |
| Burst | none | standard | |
| Calculate | none | standard | |
| EnvFollow | none | standard | |
| GameOfLife | none | standard | |
| GateDelay | none | standard | |
| GatedVCA | none | standard | `OnDataRequest()` returns 0 |

### Batch 1c: trivial tail (8)

| Applet | Vendor deps | Test shape | Notes |
| --- | --- | --- | --- |
| RndWalk | none | standard | |
| Schmitt | none | standard | |
| ShiftGate | none | standard | |
| Slew | none | standard | |
| Stairs | none | model-multiplier | Per pilot Stairs ST3 |
| TLNeuron | none | standard | |
| Trending | none | standard | |
| Voltage | none | standard | Pack helper zeroes bit 9 |

### Batch 2: vec-osc family (3)

| Applet | Vendor deps | Test shape | Notes |
| --- | --- | --- | --- |
| VectorEG | vec_osc headers (no .cpp link) | standard | Mirrors pilot VectorLFO |
| VectorMod | vec_osc headers | standard | |
| VectorMorph | vec_osc headers | standard | |

### Batch 3: quantizer family (13)

All use `HS::Quantize()` on the shim base class. No new shim surface.

| Applet | Vendor deps | Test shape | Notes |
| --- | --- | --- | --- |
| DualQuant | quant headers | standard | |
| OffsetQuant | quant headers | standard | |
| MultiScale | quant headers | standard | Uses OC::Strings::scale_names |
| ScaleDuet | quant headers | standard | |
| EnsOscKey | quant headers | standard | |
| Calibr8 | quant headers | standard | |
| Carpeggio | quant headers + hem_arp_chord.h | standard | |
| Chordinator | quant headers | standard | |
| EnigmaJr | enigma headers, quant, HSMIDI shim | standard | Shared `user_turing_machines[40]` is vendor-by-design singleton |
| Pigeons | quant headers | standard | |
| Squanch | quant headers | standard | |
| Shredder | quant headers | standard | |
| Strum | quant headers | standard | |

### Batch 4: clock-mgr + helper cat-C (8)

Per Gate C, each row's test coverage shape is per-applet, not
per-family.

| Applet | Vendor deps | Test shape | Notes |
| --- | --- | --- | --- |
| Metronome | HSClockManager (global `clock_m`) | bus-level safe | Animation only; no Clock-driven accumulators |
| ResetClock | OC::CORE::ticks | state-injection only | `ticks_since_clock` accumulator multiplies 10x per buffer; bus-level fire-count assertions are unsound. Reuse `step_n_inner_ticks` time-injection helper |
| Shuffle | OC::CORE::ticks | state-injection only | `next_trigger = tick + delay_ticks` scheduled inside Clock branch; bus-level assertions misfire |
| Xfader | none | bus-level safe | Uses `millis()` gate; collapses 10x inner loop |
| Scope | OC::CORE::ticks | model-multiplier | `last_bpm_tick` stomped 10x; assert final-tick value only |
| ClkToGate | none | model-multiplier | `ClockOut(ch)` fires 10x per edge; assert presence not count |
| ClockSkip | none | model-multiplier | RNG-rolled skip happens 10x per edge; seed or force `p_mod = 100` for determinism. `trigger_countdown[ch] = 1667` set 10x |
| PolyDiv | none | state-injection only | `Poke()` advances `clock_count` 10x per edge; division-ratio assertions need explicit modeling or state injection |

### Batch 5: cat-A misc + envelopes (5)

| Applet | Vendor deps | Test shape | Notes |
| --- | --- | --- | --- |
| ADEG | none | standard | |
| ADSREG | none | standard | `MiniADSR` inner struct; `Proportion` remains a free function (vendor calls it unqualified) |
| RunglBook | none | model-multiplier | Per pilot RunglBook RB3 |
| LowerRenz | lorenz headers + `streams_resources.o` + `streams_lorenz_generator.o` | standard | Sole applet needing non-empty `VENDOR_DEPS_LowerRenz` in Makefile (already wired) |
| Combin8 | CVInputMap (via base class) | standard | `gfxDisplayInputMapEditor()` already in shim |

## Spec footer

### Recipe spot-check

A new implementer should be able to copy ClockDivider.cpp, rename
class references, swap the manifest include, and produce a working
per-applet plug-in. Verified by audit: 6 pilot plug-ins all follow the
recipe.

### Per-entry verification

49 applet rows above are sourced from the Gate B audit. Each row's
vendor-dep claim was verified against the existing
`HemispheresFactory.h` registration of that applet (all 49 ship in
HEMI_VARIANT 0 already).

### Shim prereq verification

No new shim files. The 49 applets compile against the shim baseline.
LowerRenz alone needs explicit `.cpp` linkage at the per-applet
Makefile level; that wiring is in place.

## Verification commands

```sh
make build/arm/<APPLET>.o
arm-none-eabi-size build/arm/<APPLET>.o
make build/host/test_applet_<APPLET>
./build/host/test_applet_<APPLET>
```

## What this spec does NOT cover

- Cleanup release (delete bundled `Hemispheres.o`/`Hemispheres2.o`).
- WTVCO, DuoTET, EbbAndLfo. Need CMSIS-DSP dep port (separate phase).
- Layer 0.4 host UX rework. Tracked at
  `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md`.
