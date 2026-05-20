# Per-applet mass-port release (autonomous)

Date: 2026-05-20
Owner: Dan
Prior release: Pilot at `docs/superpowers/prompts/2026-05-19-per-applet-refactor-kickoff.md` (merged as PR #12).
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Scope

Port the remaining 49 Hemisphere applets into per-applet NT plug-ins. Pilot release shipped 6 of 56 plus 2 host plug-ins; this release covers the rest. Bundled `Hemispheres.o` and `Hemispheres2.o` continue to ship as the user transition path; cleanup release deletes them after a one-release window.

Per the kickoff convention, this release runs end-to-end through one PR. Implementer fan-out is parallelized via worktrees plus subagent dispatch per `~/.claude/rules/parallel-execution.md`.

## The two most important rules (carried from the pilot)

1. **PARALLELIZE INDEPENDENT WORK.** The 49 applet ports are independent at the file-surface level. Dispatch in parallel batches (see "Batching strategy" below). End-to-end wallclock target is roughly the time of the slowest single applet plus batch integration plus the Y-offset shim work.
2. **EVERY IMPLEMENTER WORKTREE BRANCHES FROM THE FEATURE BRANCH (`dr/per-applet-mass-port`), NOT FROM `main`.** Parent agent specifies base branch explicitly: `git worktree add <path> -b <implementer-branch> dr/per-applet-mass-port`.

## Branch and worktree

- Parent worktree: `.worktrees/per-applet-mass-port` on branch `dr/per-applet-mass-port`.
- Implementer worktrees: `.worktrees/per-applet-applet-<APPLET>` on branch `per-applet-applet/<APPLET>` where `<APPLET>` matches the vendor file name exactly (Camel/PascalCase).
- Branch from current `main` AFTER the pilot release PR #12 merges. If the pilot has not merged yet, branch from `dr/per-applet-pilot` and rebase onto `main` after merge.

Initialize submodules in the new worktree immediately after creation:

```sh
git -C .worktrees/per-applet-mass-port submodule update --init --recursive --depth=1
```

Disk usage gate: peak concurrent worktree count is 8 (Batch 1c plus parent). Each worktree with initialized submodules is ~3 GB. Verify free space before dispatch:

```sh
df -h . | awk 'NR==2 {print $4}'
```

If free space on the worktree volume is under 30 GB at any pre-batch checkpoint, halt and reclaim space before dispatch. Implementer build artifacts add another 200-500 MB per worktree at peak.

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

Default convention: opaque accessors per ProbabilityDivider pilot. Works uniformly across applets with different state shapes, keeps the test file free of vendor internals, and removes drift risk between an inline pack helper and the per-applet `.cpp`'s real serializer.

Inline `pack_<applet>` helper (per ClockDivider pilot) is allowed only when the test specifically validates the pack format itself (byte layout, bit-packing edge cases). Implementers using the inline form must justify the deviation in the commit message; default is opaque accessors.

### 7. Host-side `_NT_slot` stubs live ONLY in `harness/src/nt_runtime.cpp`

The pilot's Quadrants implementer added local stubs in `test_host_Quadrants_host.cpp` which collided with `nt_runtime.cpp`'s stubs at integration. Mass-port implementers should NOT add local `_NT_slot::*` stubs. The central stubs in `harness/src/nt_runtime.cpp` cover every host test binary.

### 8. ABORT-A2 cleared on hardware (validated in pilot smoke)

The function-pointer-in-data ABI trick was load-bearing for the entire refactor; pilot hardware smoke proved `_NT_slot::plugin()` returns a valid pointer for per-applet plug-in instances and the host calls through correctly. Mass-port does NOT need to re-validate this; treat it as a known-good substrate.

### 9. ABI version unchanged; per-instance state is implementation detail

`HemiPluginInterface` remains at version 1. The public ABI struct is unchanged across mass-port. Per-instance state added in Layer 0.1 (`last_cv[]`, `gate_prev[]`) lives in `_AppletInstance` fields, not in the public struct. Host code does not see per-instance state; only the per-applet `.cpp` and `_per_applet_runtime.h` touch it.

## Pre-Layer-0 gates (run before any setup commit)

Four explicit checks gate the entire release. All four must pass before the parent agent writes any code on `dr/per-applet-mass-port`. Failure of any gate halts and routes through the human checkpoint for replan.

### Gate A: Vendor SHA verification

The pilot built against pinned vendor SHAs. Mid-stream drift would expose implementers to a different vendor surface than the spec was written against. Verify both submodules match the pins at top of this doc:

```sh
git -C vendor/O_C-Phazerville rev-parse HEAD     # expect: 7800d929...
git -C vendor/distingNT_API   rev-parse HEAD     # expect: cd12d876...
```

Mismatch on either = halt. Investigate drift; do not silently rebase to current upstream.

### Gate B: Audit-as-gate before Batch 1 dispatch

The audit step (formerly Layer 0.7; see "Audit and verification" below) MUST complete before Batch 1a dispatch. Audit is not a side task that runs concurrently with implementers.

If audit demotes more than 5 of 49 applets to "needs shim work" or "needs vendor dep not in baseline", halt and scope-replan. High demotion rate is evidence of mis-drawn boundaries, not just narrow scope. Do not dispatch around the demotions.

### Gate C: Batch 4 per-applet 10x-ticks categorization

The pilot's ResetClock spec-mismatch retrospective showed the Cumulus 10x-ticks gotcha re-applies per applet, not per family. Audit MUST categorize each of the 8 Batch 4 applets (Metronome, ResetClock, Shuffle, Xfader, Scope, ClkToGate, ClockSkip, PolyDiv) individually on these axes:

1. Does the applet call `if (Clock(ch))` accumulators inside `Controller()`?
2. Does the applet depend on per-tick timing inside `Controller()` (e.g., counts inner ticks for swing or division)?
3. Does the applet hold an internal phase or accumulator that the host harness's 10x inner-tick loop will multiply?

Each Batch 4 implementer prompt carries the audit-derived categorization for its applet. Generic "clock-mgr family" labels are insufficient; the gotcha is per-applet.

### Gate D: File-scope mutable state audit per applet

Layer 0.1 fixes `last_cv` and `gate_prev` in the runtime. Vendor applets themselves may carry file-scope mutable state (static buffers, accumulator arrays at namespace scope) that break under two-instance hosting. For each of the 49 in-scope applets:

```sh
grep -nE '^(static [^c]|namespace [a-z]+ \{)' vendor/O_C-Phazerville/software/src/applets/<APPLET>.h
```

Categorize each match:

- **Applet-private**: variable lives logically inside the applet class; safe (lives in `_AppletInstance`).
- **Shared with vendor base**: variable shared across instances; needs Layer 0.1-style per-instance treatment. Document and route the fix through Layer 0.1 before dispatch.
- **Singleton-by-design**: shared across instances intentionally (e.g., ProbabilityDivider's `ProbLoopLinker`). Document the constraint in the spec; user-facing implication noted in PR body.

Without this gate, mass-port surfaces a Layer 0.1-class bug 49x across the codebase that won't appear until a user puts two of the same applet in a Quadrants host.

## Pre-mass-port refactor work (parent agent, sequential, Layer 0)

The pilot left four known gaps. Land these on `dr/per-applet-mass-port` BEFORE implementer fan-out (and after all four Pre-Layer-0 gates pass):

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

### Layer 0.4: Host slot-selector UX rework + parameter proxying — DEFERRED

Cut from mass-port. Tracked at `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md` as its own release. The 49 applet ports do not depend on any of the four sub-pieces (enum slot selector, proxy param aggregation, `NT_updateParameterDefinition` refresh on slot change, `parameterChanged` forwarding); applets ship with the current raw-slot-index host UI.

First task in the follow-up release is verifying `NT_setParameterFromUi`-from-`parameterChanged` reentrancy semantics on hardware. The proxy design depends on the answer; it must be settled before any host-side coding starts. Mass-port does not touch this.

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

49 applets in one mass dispatch is too many concurrent agents. Batch by class for better parallelism control. Batch 1 splits into a canary plus two follow-ups so the first higher-N dispatch validates the path before committing to size:

| Batch | Applets | Count | Why grouped |
|---|---|---|---|
| Batch 1a: trivial canary | AttenuateOffset, Binary, Button, Logic, Switch | 5 | Smallest text; no vendor deps; validates the higher-N dispatch path before scaling |
| Batch 1b: trivial cont. | Brancher, Burst, Calculate, EnvFollow, GameOfLife, GateDelay, GatedVCA | 7 | No vendor deps; dispatched only after 1a integrates clean |
| Batch 1c: trivial tail | RndWalk, Schmitt, ShiftGate, Slew, Stairs, TLNeuron, Trending, Voltage | 8 | No vendor deps; dispatched only after 1b integrates clean |
| Batch 2: vec-osc family | VectorEG, VectorMod, VectorMorph | 3 | Share vendor vec-osc surface (already in baseline) |
| Batch 3: quantizer family | DualQuant, OffsetQuant, MultiScale, ScaleDuet, EnsOscKey, Calibr8, Carpeggio, Chordinator, EnigmaJr, Pigeons, Squanch, Shredder, Strum | 13 | Share `HS::Quantize` pool |
| Batch 4: clock-mgr / helper-using cat-C | Metronome, ResetClock, Shuffle, Xfader, Scope, ClkToGate, ClockSkip, PolyDiv | 8 | Share clock-mgr surface + step_n_inner_ticks helper |
| Batch 5: cat-A misc / envelopes | ADEG, ADSREG, RunglBook, LowerRenz, Combin8 | 5 | Mixed; LowerRenz needs lorenz objs (only one in this batch); others trivial |

Total: 5 + 7 + 8 + 3 + 13 + 8 + 5 = 49. Confirmed.

Canary rationale: pilot dispatched 6 implementers in one batch successfully; mass-port has no prior data above N=6. Batch 1a at N=5 stays within pilot-validated range. If 1a integrates clean (no spec defects surface), 1b at N=7 confirms the pattern holds slightly above pilot. 1c at N=8 sets the post-canary ceiling for subsequent batches. If 1a surfaces a spec defect, fix and re-dispatch before 1b; this is the cheapest place to catch a 49-multiplied bug.

Batch 4 special handling: pilot ResetClock spec-mismatch retrospective showed the 10x-ticks-per-step gotcha re-applies per applet, not per family. Audit step (see Pre-Layer-0 gates) categorizes each Batch 4 applet individually: does it call `Clock(ch)` accumulators? Does it depend on per-tick timing inside `Controller()`? Each Batch 4 implementer prompt carries the audit-derived categorization for its applet; generic "clock-mgr family" labels are insufficient.

Dispatch each batch in a single message (multiple Agent tool calls). Sequential batches; each batch finishes and integrates before next dispatches. Per-batch soft ceiling 2 hours; per-batch abort if more than 25% of implementers fail their first run (signals spec defect, not implementer error).

Wallclock target: pilot's 6-implementer batch averaged ~10-15 min per implementer wall clock. Mass-port batches finish in roughly the slowest implementer in each batch. End-to-end target: ~60-90 minutes per batch plus integration overhead.

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
  - _per_applet_runtime.h changed in Layer 0.1: populate_frame_from_bus takes per-instance
    state pointers. Wire inst->last_cv[] and inst->gate_prev[] in construct() before any
    call into the runtime. File-scope statics will corrupt under two-instance hosting.
  - Cumulus-style 10x ticks-per-step gotcha applies to clock-driven applets.
  - Test seam: extern "C" opaque accessors for vendor state introspection (default per Lesson 6).
    Inline pack helper allowed only when test validates pack format; justify in commit message.

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
- `plugins/hosts/Hemispheres_host.cpp` updates: pass `gfx_offset_y = 0` for the existing left/right layout. Slot selector UI unchanged (raw slot index); enum-scroll deferred to follow-up release.
- `plugins/hosts/Quadrants_host.cpp` updates: per-slot `origin_y` wiring so Y-offset shim activates the bottom row. Slot selector UI unchanged; enum-scroll deferred.
- Test updates: existing host tests pass; add Quadrants Y-render coverage now that the shim supports it.
- `Makefile`: `PILOT_APPLET_LIST` becomes `ALL_APPLET_LIST` (or similar) with all 55 ported applets enumerated.
- Spec doc: `docs/superpowers/specs/2026-05-20-per-applet-mass-port-design.md` with corrected templates per Lesson sections above.
- Brainstorm: `docs/superpowers/brainstorms/2026-05-20-per-applet-mass-port-brainstorm.md` with per-applet status (49 in-scope; per-batch grouping; vendor dep accounting).
- Plan: `docs/superpowers/plans/2026-05-20-per-applet-mass-port-plan.md` with batched parallel execution.
- Follow-up kickoff stub: `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md` carries the deferred Layer 0.4 scope.
- `CLAUDE.md` updates: per-applet pilot architecture is now primary path; reflect ABI status (HemiPluginInterface v1 unchanged; per-instance state lives in `_AppletInstance`, not the public struct).

## Wallclock structure and human checkpoints

End-to-end Layer 0 + 7 batches + smoke + PR runs 10-12 hours unattended. Too long for one autonomous run. Three human checkpoints structure the release:

| Checkpoint | When | Type | Action |
|---|---|---|---|
| Post-Layer-0 | All Layer 0 setup commits landed; `make test-applets` and `make arm` green | Hard halt | Human reviews setup; approves Layer 1 dispatch |
| Per-batch | Batch cherry-picked; tests + arm size table posted | Soft halt | Auto-proceed to next batch if clean; hard halt on any red |
| Post-hardware-smoke | All 49 deployed; cross-batch preset verified | Hard halt | Human approves PR open |

Per-batch soft ceiling: 2 hours wallclock. Per-batch abort threshold: more than 25% of implementers fail first run. Either triggers halt-and-reassess rather than churn.

Layer 0 is the highest-risk segment (shim changes touch every applet's runtime contract). The hard halt is non-negotiable; do not skip even if Layer 0 appears clean.

## Integration loop (Layer 2)

After each batch:

1. Cherry-pick the batch's implementer commits onto `dr/per-applet-mass-port` in completion order (first finished, first picked). Alphabetical reordering wastes time. A conflict on any pick is the signal to investigate that implementer, not to reorder.
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

- Any Layer 0 prerequisite work (per-instance state, gfx_offset_y, per-output mode names) fails to land cleanly in 2 attempts.
- More than 5 of 49 implementers fail the build/test loop on their first run. Indicates spec or shim defect rather than implementer error.
- Both-hosts-loaded preset overflows ITC at smoke; need scope replan (drop the both-hosts certification, accept as documented constraint).
- The Y-offset shim work breaks any existing applet's render (regression in bundled host visible at hardware smoke).
