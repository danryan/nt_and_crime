# Per-applet mass-port release (autonomous)

Date: 2026-05-20
Owner: Dan
Prior release: Pilot at `docs/superpowers/prompts/2026-05-19-per-applet-refactor-kickoff.md` (merged as PR #12).
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Scope

Port the remaining 49 Hemisphere applets into per-applet NT plug-ins. Pilot release shipped 6 of 56 plus 2 host plug-ins; this release covers the rest. Bundled `Hemispheres.o` and `Hemispheres2.o` continue to ship as the user transition path; cleanup release deletes them after a one-release window.

Per the kickoff convention, this release runs end-to-end through one PR. Implementer fan-out is parallelized via worktrees plus subagent dispatch per `~/.claude/rules/parallel-execution.md`.

## The two most important rules (carried from the pilot)

1. **PARALLELIZE INDEPENDENT WORK.** The 49 applet ports are independent at the file-surface level. Dispatch in parallel batches (see "Batching strategy" below). End-to-end wallclock target is roughly the time of the slowest single applet plus batch integration plus host UX rework plus the Y-offset shim work.
2. **EVERY IMPLEMENTER WORKTREE BRANCHES FROM THE FEATURE BRANCH (`dr/per-applet-mass-port`), NOT FROM `main`.** Parent agent specifies base branch explicitly: `git worktree add <path> -b <implementer-branch> dr/per-applet-mass-port`.

## Branch and worktree

- Parent worktree: `.worktrees/per-applet-mass-port` on branch `dr/per-applet-mass-port`.
- Implementer worktrees: `.worktrees/per-applet-applet-<APPLET>` on branch `per-applet-applet/<APPLET>` where `<APPLET>` matches the vendor file name exactly (Camel/PascalCase).
- Branch from current `main` AFTER the pilot release PR #12 merges. If the pilot has not merged yet, branch from `dr/per-applet-pilot` and rebase onto `main` after merge.

Initialize submodules in the new worktree immediately after creation:

```sh
git -C .worktrees/per-applet-mass-port submodule update --init --recursive --depth=1
```

## Model selection

- Parent orchestrator: `opus` (this is what you are).
- Implementer subagents: `sonnet`.
- Audit subagents (for vendor dep walks, applet categorization): `sonnet`.
- Code-review pass (post-integration, pre-merge): `sonnet` via `pr-review-toolkit:review-pr` or equivalent.

## Pilot lessons (apply throughout mass-port)

These corrections supersede any conflicting guidance in the pilot kickoff or pilot spec. Each is load-bearing; do NOT re-discover them during fan-out.

### 1. Manifest is a struct inside `namespace per_applet`, NOT a nested namespace

The `_per_applet_runtime.h` templates take a TYPE parameter (`typename ManifestNS`). Namespaces cannot be used as template arguments.

```cpp
namespace per_applet {
struct <APPLET> {
    static constexpr uint32_t      guid        = NT_MULTICHAR('H','m','X','x');
    static constexpr const char*   name        = "...";
    static constexpr const char*   description = "...";
    static constexpr BusParam      inputs[]    = { ... };
    static constexpr BusParam      outputs[]   = { ... };
    static_assert((guid & 0xFFFF) == kHemiGuidPrefix, "Hemi applet guid must start with 'Hm'");
};
}
```

### 2. Per-applet `.cpp` MUST set `HS::gfx_offset = origin_x` in `render_view_impl`

The pilot's first hardware deployment showed only one of two slots rendering in the Hemispheres host because vendor `View()` draws relative to `HS::gfx_offset`. The fix lives in each per-applet `render_view_impl`:

```cpp
static void render_view_impl(_NT_algorithm* self, int origin_x, int /*origin_y*/) {
    HS::gfx_offset = origin_x;
    static_cast<_AppletInstance*>(self)->applet.View();
    HS::gfx_offset = 0;
}
```

This MUST be in every per-applet plug-in. Mass-port subagents should be told this explicitly.

Mass-port should also lift this boilerplate into `_per_applet_runtime.h` as a templated helper so individual applet `.cpp` files just delegate:

```cpp
// In _per_applet_runtime.h:
template <typename AppletInstance>
inline void render_view_with_offset(_NT_algorithm* self, int origin_x) {
    HS::gfx_offset = origin_x;
    static_cast<AppletInstance*>(self)->applet.View();
    HS::gfx_offset = 0;
}
```

### 3. Per-applet `.cpp` include order (avoid ODR conflicts)

The pilot's six implementers each found their own order to dodge ODR errors. Canonical order:

```cpp
#include "../../shim/include/HemisphereApplet.h"  // base class first
#include "../../shim/include/PhzIcons.h"
#include "../../shim/include/Arduino.h"
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/<APPLET>.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/<APPLET>.h"
#include "_per_applet_runtime.h"
```

Do NOT include `hemispheres_shim.h`. It pulls in `HemispheresFactory.h` which includes all 56 vendor applet headers; the resulting double-include violates ODR for whichever applet you are building.

### 4. Manifest struct vs vendor class name collision — alias the vendor class

When the vendor applet's class name collides with the manifest struct name, alias one of them:

```cpp
namespace { using ManifestNS = per_applet::<APPLET>; }   // for the template
// vendor class is `::APPLET` (global)
struct _AppletInstance : public HemiPluginInterface {
    ::<APPLET> applet;   // explicit `::` qualifies the global vendor class
};
```

### 5. Test seam: per-applet `.cpp` exposes `extern "C"` opaque accessors

Per-applet test files cannot include the vendor applet header (ODR collision with the per-applet `.cpp`'s include). Define `extern "C"` accessors in each per-applet `.cpp`:

```cpp
extern "C" uint64_t <applet>_on_data_request(_NT_algorithm* self);
extern "C" void     <applet>_on_data_receive(_NT_algorithm* self, uint64_t state);
extern "C" <APPLET>* get_<applet>_applet(_NT_algorithm* self);  // for state introspection
```

These functions live in the same TU as `_AppletInstance` so they can cast safely.

### 6. `applet_test_helpers.cpp` is NOT linked into per-applet test binaries

Per-applet host tests inline the `pack_<applet>` helper locally (per ClockDivider pilot) OR use opaque accessors (per ProbabilityDivider pilot). Pick one convention per applet; document at the top of the test file.

### 7. Host-side `_NT_slot` stubs live ONLY in `harness/src/nt_runtime.cpp`

The pilot's Quadrants implementer added local stubs in `test_host_Quadrants_host.cpp` which collided with `nt_runtime.cpp`'s stubs at integration. Mass-port implementers should NOT add local `_NT_slot::*` stubs. The central stubs in `harness/src/nt_runtime.cpp` cover every host test binary.

### 8. ABORT-A2 cleared on hardware (validated in pilot smoke)

The function-pointer-in-data ABI trick was load-bearing for the entire refactor; pilot hardware smoke proved `_NT_slot::plugin()` returns a valid pointer for per-applet plug-in instances and the host calls through correctly. Mass-port does NOT need to re-validate this; treat it as a known-good substrate.

## Pre-mass-port refactor work (parent agent, sequential, Layer 0)

The pilot left four known gaps. Land these on `dr/per-applet-mass-port` BEFORE implementer fan-out:

### Layer 0.1: Per-instance shared state in `_per_applet_runtime.h`

Pilot's `last_cv_storage()` and `gate_prev(channel)` use file-scope static arrays. Move them to `_AppletInstance` per-instance fields. Update `populate_frame_from_bus<ManifestNS>` to take per-instance state pointers.

This is the only way two instances of the same applet (e.g., two Compares in one preset) work correctly side-by-side under a host. Without this, mass-port presets that put two of the same applet in a Quadrants host will corrupt `changed_cv` detection.

### Layer 0.2: `render_view_with_offset` helper in `_per_applet_runtime.h`

Replace the per-applet `render_view_impl` boilerplate with a templated helper (see Lesson 2 above). Mass-port implementers should call:

```cpp
inst->render_view = &per_applet_runtime::render_view_with_offset<_AppletInstance>;
```

instead of writing their own `render_view_impl`.

### Layer 0.3: `"Output mode"` per-output naming

Replace the generic `"Output mode"` parameter name in `_per_applet_runtime.h::emit_base_parameters` with `"<output name> mode"`. Use a static `const char` storage scheme: generate the per-output mode names at compile time via a per-manifest static array. Implementation sketch:

```cpp
// In manifest:
struct <APPLET> {
    static constexpr const char* output_mode_names[] = { "Out A mode", "Out B mode" };
    // ...
};
```

OR runtime concatenation into static storage (uses minimal heap-free assembly). Pick one; document in spec.

### Layer 0.4: Host slot-selector UX rework + parameter proxying

**Selector**: replace the `Slot N index` uint8 parameter with an enum that scrolls through Hemi-prefix algorithms in the preset.

1. At each `draw()`, scan `NT_algorithmCount()` slots and build a list of `{slot_index, name}` for any algorithm with guid prefix `'Hm'`.
2. Use `NT_updateParameterDefinition` to refresh the parameter's enum strings to that list.
3. The parameter value is now an enum index into that list, not a raw slot index.
4. `resolve_slot` is called with the actual slot_index resolved from the enum index.

**Parameter proxying**: aggregate each watched slot's `_NT_parameter[]` table into the host's parameter page so the user can adjust per-applet bus mappings + applet-specific params from the host's algo view without navigating away.

1. Host `calculateRequirements`: budget `numParameters = kNumSlotIndexParams + kMaxProxyParamsPerSlot * kNumSlots` (e.g. 2 selectors plus 16 proxies times 2 slots = 34 for Hemispheres host; 4 selectors plus 16 times 4 = 68 for Quadrants).
2. Host `construct`: after slot indices known, call `NT_getSlot(slot, watched_idx)` for each watched slot, iterate `slot.numParameters()`, copy via `slot.parameterInfo(info, p)` into the host's parameter table starting at offset `kNumSlotIndexParams`. Prefix each proxy name with `"S0:"` / `"S1:"` so the user can tell which slot owns it.
3. Host `parameterChanged(host_param_idx)`: if the changed parameter is a proxy, decode the `(slot_idx, slot_param_idx)` mapping and call `NT_setParameterFromUi(slot_idx, slot_param_idx, host->v[host_param_idx])` to forward the edit.
4. Slot-index changes trigger a `NT_updateParameterDefinition` refresh so the proxy params re-resolve to the new slot's params.

This applies to both Hemispheres host and Quadrants host. Quadrants ALSO requires the Y-offset shim work (Layer 0.5).

### Layer 0.5: Y-offset support in applet shim (Quadrants prerequisite)

`HemisphereApplet::gfxPos`, `gfxFrame`, `gfxLine`, etc. add `gfx_offset` (an X-only int) to the X coordinate. Quadrants needs to stack 4 applets in a 2x2 grid; the bottom-row applets need their draws shifted in Y. Add `HS::gfx_offset_y` (int, default 0) parallel to `gfx_offset`, and update every `gfx*` helper in `HemisphereApplet.h` to add it to Y:

```cpp
namespace HS { extern int gfx_offset_y; }
// ...
void gfxPos(int x, int y) { graphics.setPrintPos(x + gfx_offset, y + gfx_offset_y); }
void gfxFrame(int x, int y, int w, int h) { graphics.drawFrame(x + gfx_offset, y + gfx_offset_y, w, h); }
// ... (and the rest)
```

Then each per-applet `render_view_impl` (or its `render_view_with_offset` helper) sets both `HS::gfx_offset = origin_x` AND `HS::gfx_offset_y = origin_y`.

Quadrants becomes functional once this lands. Pilot Quadrants registered + routed events correctly but could not render bottom-row applets; this fixes the rendering side.

### Layer 0.6: Update `_per_applet_runtime.h` test seams + spec corrections

Re-author `docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md` corrections (or create a new mass-port spec doc) to reflect lessons 1-7 above. The mass-port implementer prompts must reference the corrected spec.

## Audit and verification (Layer 0.7)

Before implementer fan-out, audit each of the 49 remaining applets:

1. Walk transitive `#include` graph for each vendor applet header.
2. Identify vendor dep linkages: which applets need `VENDOR_DEPS_<APPLET>` non-empty?
   - Known: LowerRenz needs lorenz objs (`streams_resources.o`, `streams_lorenz_generator.o`).
   - Quantizer applets need `HS::Quantize` pool (already in shim baseline via Phase 5 dep-quant).
   - Class B/C/D applets per Phase 6 brainstorm have specific dep needs already cataloged.
3. Categorize per Phase 6 brainstorm classes (A/B/C/D) or simpler: "trivial" (no deps), "vendor-dep", "vendor-singleton" (analog of ProbabilityDivider).
4. Halt and replan if any audit finds:
   - Vendor dep not in shim baseline (would require shim work, out of mass-port scope).
   - Applet uses a vendor API not exposed by the per-applet runtime (e.g. needs raw I2C bus access).
   - Combined post-port `.text` budget per slot exceeds ITC pool capacity (~100 KB total ITC across loaded slots).

## Batching strategy (Layer 1: implementer fan-out)

49 applets in one mass dispatch is too many concurrent agents. Batch by class for better parallelism control:

| Batch | Applets | Count | Why grouped |
|---|---|---|---|
| Batch 1: trivial / Phase 4 cat-A | AttenuateOffset, Binary, Brancher, Burst, Button, Calculate, EnvFollow, GameOfLife, GateDelay, GatedVCA, Logic, RndWalk, Schmitt, ShiftGate, Slew, Stairs, Switch, TLNeuron, Trending, Voltage | 20 | No vendor deps; smallest text; safest batch |
| Batch 2: vec-osc family | VectorEG, VectorMod, VectorMorph | 3 | Share vendor vec-osc surface (already in baseline) |
| Batch 3: quantizer family | DualQuant, OffsetQuant, MultiScale, ScaleDuet, EnsOscKey, Calibr8, Carpeggio, Chordinator, EnigmaJr, Pigeons, Squanch, Shredder, Strum | 13 | Share `HS::Quantize` pool |
| Batch 4: clock-mgr / helper-using cat-C | Metronome, ResetClock, Shuffle, Xfader, Scope, ClkToGate, ClockSkip, PolyDiv | 8 | Share clock-mgr surface + step_n_inner_ticks helper |
| Batch 5: cat-A misc / envelopes | ADEG, ADSREG, RunglBook, LowerRenz, Combin8 | 5 | Mixed; LowerRenz needs lorenz objs (only one in this batch); others trivial |

Total: 20 + 3 + 13 + 8 + 5 = 49. Confirmed.

Dispatch each batch in a single message (multiple Agent tool calls). Sequential batches; each batch finishes + integrates before next dispatches. Allows targeted recovery from any failing implementer without blocking the whole fan-out.

Wallclock target: pilot's 6-implementer batch averaged ~10-15 min per implementer wall clock. Mass-port batches of 20, 13, 8, 5, 3 should finish in roughly the slowest implementer in each batch, gated on the same wall-clock per applet. End-to-end target: ~90 minutes per batch + integration overhead.

## Per-implementer prompt template

Each implementer subagent receives:

```
WORKING DIRECTORY: .worktrees/per-applet-applet-<APPLET>
BRANCH: per-applet-applet/<APPLET>
SLUG: <APPLET>
APPLET: <APPLET>

FIRST ACTION (verify worktree before reading anything):
  cd <worktree path>
  pwd
  git rev-parse --abbrev-ref HEAD                  # expect: per-applet-applet/<APPLET>
  test -f docs/superpowers/specs/2026-05-20-per-applet-mass-port-design.md && echo SPEC_PRESENT
  test -f plugins/applets/_per_applet_runtime.h && echo RUNTIME_PRESENT
  test -f shim/include/HemiPluginInterface.h && echo ABI_PRESENT
  git submodule status | head -2

WHAT TO BUILD:
  1. shim/include/applet_manifests/<APPLET>.h   (struct per_applet::<APPLET>; guid; inputs; outputs)
  2. plugins/applets/<APPLET>.cpp                (per-applet plug-in per spec template)
  3. harness/tests/test_applet_<APPLET>.cpp      (round-trip + behavior + customUi coverage)

KEY SPEC POINTS (read the spec; these are the load-bearing reminders):
  - Manifest is STRUCT inside namespace per_applet, NOT a nested namespace.
  - Per-applet .cpp uses canonical include order (HemisphereApplet first, then PhzIcons, Arduino,
    HemiPluginInterface, manifest, vendor applet, _per_applet_runtime LAST).
  - render_view wired to per_applet_runtime::render_view_with_offset<_AppletInstance> (or
    if writing impl directly, it MUST set HS::gfx_offset = origin_x).
  - Cumulus-style 10x ticks-per-step gotcha applies to clock-driven applets.
  - Test seam: extern "C" opaque accessors for vendor state introspection.

ALLOWED SURFACE:
  plugins/applets/<APPLET>.cpp
  shim/include/applet_manifests/<APPLET>.h
  harness/tests/test_applet_<APPLET>.cpp

FORBIDDEN: every other file. Pre-commit hook hard-rejects.

VERIFICATION:
  make build/arm/<APPLET>.o 2>&1 | tail -10
  arm-none-eabi-size build/arm/<APPLET>.o
  make build/host/test_applet_<APPLET> 2>&1 | tail -5
  ./build/host/test_applet_<APPLET>

COMMIT:
  git add plugins/applets/<APPLET>.cpp shim/include/applet_manifests/<APPLET>.h harness/tests/test_applet_<APPLET>.cpp
  git commit -m "feat(plugins): port <APPLET> as standalone per-applet plug-in"

REPORT BACK: worktree, branch, commit SHA, arm-none-eabi-size output (.text/.data/.bss), test count.
```

## Pre-commit hook update

Add `per-applet-applet/<any>` rule + `dr/per-applet-mass-port` integration rule to `.git/hooks/pre-commit`. Reuse the pilot's pattern (the file at `.git/hooks/pre-commit` already handles `per-applet-applet/*` correctly; just add the new feature branch to the ancestry whitelist).

Update the hook BEFORE Layer 1 fan-out. Without this, implementer subagents will hit the existing pilot ancestry check (`dr/per-applet-pilot`) which may fail if mass-port branched off `main` post-pilot-merge.

## Setup commit file inventory (Layer 0)

- `_per_applet_runtime.h` updates: per-instance state, `render_view_with_offset` helper, per-output mode name.
- `shim/include/HemisphereApplet.h` updates: `HS::gfx_offset_y` declaration and use throughout `gfx*` helpers.
- `shim/src/globals.cpp` updates: `gfx_offset_y` definition.
- `plugins/hosts/Hemispheres_host.cpp` updates: enum-scroll slot selector + use of `gfx_offset_y` (set to 0 for left/right layout).
- `plugins/hosts/Quadrants_host.cpp` updates: enum-scroll slot selector + per-slot origin_y wiring.
- Test updates: existing host tests pass; add Quadrants Y-render coverage now that the shim supports it.
- `Makefile`: `PILOT_APPLET_LIST` becomes `ALL_APPLET_LIST` (or similar) with all 55 ported applets enumerated.
- Spec doc: `docs/superpowers/specs/2026-05-20-per-applet-mass-port-design.md` with corrected templates per Lesson sections above.
- Brainstorm: `docs/superpowers/brainstorms/2026-05-20-per-applet-mass-port-brainstorm.md` with per-applet status (49 in-scope; per-batch grouping; vendor dep accounting).
- Plan: `docs/superpowers/plans/2026-05-20-per-applet-mass-port-plan.md` with batched parallel execution.

## Integration loop (Layer 2)

After each batch:

1. Cherry-pick the batch's implementer commits onto `dr/per-applet-mass-port` in alphabetical order.
2. `make test-applets` clean. Existing baseline plus per-applet additions all green.
3. `make arm` clean.
4. `arm-none-eabi-size` table appended to running PR body draft.
5. If any cherry-pick conflicts (should be rare; per-applet files are non-overlapping), investigate the failing implementer rather than blindly resolving.

## Hardware smoke (Layer 3)

After all 49 applets integrated:

- Deploy every per-applet `.o` via sysex. Confirm all show Passed in View Info.
- Build a Hemispheres host preset with 2 random pilots from different batches. Verify cross-batch interoperability.
- Build a Quadrants host preset with 4 pilots (now functional given Y-offset shim work). Confirm 2x2 grid renders correctly.
- Both-hosts-loaded stress preset (Hemispheres + 2 + Quadrants + 4 = 6 algorithms loaded). Read total ITC; certify or document constraint.

## PR (Layer 4)

Open PR titled "Per-applet refactor mass-port release (49 applets)" with:

- Per-batch `.text` size tables.
- ITC consumption table.
- Pilot-vs-mass-port comparison (combined ITC budget).
- Test plan checkboxes mirroring pilot.
- Note: cleanup release (delete bundled `Hemispheres.o`/`Hemispheres2.o`) follows after a one-release transition window. Authored as the next kickoff doc.

## What this prompt does NOT cover

- Cleanup release (`Hemispheres.o`/`Hemispheres2.o` deletion). Authored at end of mass-port.
- WTVCO, DuoTET, EbbAndLfo (Phase 6 deferrals). Need CMSIS-DSP dep port; separate dep-port phase.

## Abort conditions

Halt and post abort report under `docs/superpowers/abort-reports/`:

- Any Layer 0 prerequisite work (per-instance state, gfx_offset_y, host UX rework) fails to land cleanly in 2 attempts.
- More than 5 of 49 implementers fail the build/test loop on their first run. Indicates spec or shim defect rather than implementer error.
- Both-hosts-loaded preset overflows ITC at smoke; need scope replan (drop the both-hosts certification, accept as documented constraint).
- The Y-offset shim work breaks any existing applet's render (regression in bundled host visible at hardware smoke).
