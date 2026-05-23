# Brainstorm: Per-applet refactor pilot release

Date: 2026-05-19
Status: Active
Owner: Dan
Branch: `dr/per-applet-pilot`
Worktree: `.worktrees/per-applet-pilot`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.
Kickoff: `docs/superpowers/prompts/2026-05-19-per-applet-refactor-kickoff.md`.

## Scope

Pilot release of the per-applet refactor. Unbundle 6 of the 56 Hemisphere applets into one NT plug-in per applet (`plugins/applets/<APPLET>.cpp` → `build/arm/<APPLET>.o`) and introduce two thin host plug-ins (`plugins/hosts/Hemispheres.cpp` and `plugins/hosts/Quadrants.cpp`) that compose per-applet plug-ins at runtime through the firmware slot API plus a versioned function-pointer-in-data interface stored on each algorithm instance (`HemiPluginInterface`).

The bundled `Hemispheres.o` plus `Hemispheres2.o` continue to ship throughout this pilot release. Cleanup of the bundled artifacts is cleanup-release scope, two releases out.

## Goals of the pilot release

1. Prove the per-applet packaging shape (one `.o` per applet, 16-20 KB `.text` budget each, well under the firmware's ~82 KB per-`.o` scan-time cap).
2. Validate the `HemiPluginInterface` ABI: magic (`'HMI1'`) + version (1) + 5 function pointers stored as data on the algorithm instance. Hosts validate before calling through.
3. Validate the NT control-claim model via `hasCustomUi`/`customUi` for three configurations: standalone per-applet (encoderL + encoderButtonL + button1); Hemispheres host (both encoders + buttons + button1/2); Quadrants host (both encoders + buttons + button1-4 for direct slot select).
4. Measure runtime ITC consumption with realistic preset configurations: Hemispheres host + 2 applets, Quadrants host + 4 applets, and the both-hosts-loaded stress configuration.
5. Freeze the per-applet manifest schema so the mass-port release can fan out 50 implementers against a stable target.

## Pilot applet selection (6)

| Pilot | Why this applet | Vendor file |
|---|---|---|
| Compare | Smallest unique text (0 bytes per solo probe). Baseline plumbing validator. | `applets/Compare.h` |
| ClockDivider | Typical small applet. Exercises clock-divider logic against standalone control claim. | `applets/ClockDivider.h` |
| VectorLFO | Exercises vendor deps (VectorOscillator, WaveformManager, tideslite). Confirms vendor dep accounting per `.o`. | `applets/VectorLFO.h` |
| Cumulus | Exercises HS::frame writes plus the 10x ticks-per-step gotcha. Validates `_per_applet_runtime` step wrapper. | `applets/Cumulus.h` |
| Relabi | Fattest pilot (4204 B unique per solo probe). Validates per-applet pattern under worst case. RelabiManager + SegmentDisplay coverage. | `applets/Relabi.h` |
| ProbabilityDivider | Exercises HSProbLoopLinker singleton (header-only, vendor-located). Documents `.o`-private-singleton semantics for future ProbabilityMelody port. | `applets/ProbabilityDivider.h` |

These six exercise the four orthogonal axes the refactor must work for: bus I/O only (Compare), encoder/button claim (ClockDivider), vendor dep linkage (VectorLFO), the 10x clocked multiplier (Cumulus), large-text + RelabiManager (Relabi), and singleton-by-translation-unit (ProbabilityDivider).

## Vendor dep accounting (post-audit)

All 6 pilots have empty `VENDOR_DEPS_<APPLET>` Makefile variables. Detailed walk of the transitive `#include` graph from each applet header:

- Compare: no includes; empty deps.
- ClockDivider: `#include "../util/clkdivmult.h"`. Header is in baseline (`shim/include/util/clkdivmult.h`). Header-only. Empty deps.
- VectorLFO: `#include "../vector_osc/HSVectorOscillator.h" "../vector_osc/WaveformManager.h" "../tideslite.h"`. All three already re-exported under `shim/include/vector_osc/` and `shim/include/tideslite/`. `tideslite.cpp` is not called (VectorLFO uses only `constexpr ComputePhaseIncrement`). Empty deps.
- Cumulus: no includes; empty deps.
- Relabi: `#include "../HSRelabiManager.h" "../vector_osc/HSVectorOscillator.h" "../vector_osc/WaveformManager.h"`. RelabiManager is header-only and vendor-located (no shim re-export needed; resolves through `-Ivendor/.../applets` via the `..` path). vector_osc surface re-exported by shim. Empty deps.
- ProbabilityDivider: `#include "../HSProbLoopLinker.h"`. Header-only; resolves through `-Ivendor/.../applets` via `..` to `vendor/.../src/HSProbLoopLinker.h`. Empty deps.

This contradicts the kickoff prompt's `VENDOR_DEPS_VectorLFO` and `VENDOR_DEPS_Relabi` assignments (each listed two lorenz `.o` files). The lorenz path is unreachable from either include graph. Adopt the audit-confirmed empty assignments in the spec.

## ABI audit outcomes

Three of the four abort conditions cleared from header inspection:

| Condition | Status | Citation |
|---|---|---|
| ABORT-A1 (`hasCustomUi`/`customUi` not factory members) | Cleared | `vendor/distingNT_API/include/distingnt/api.h:475-476` |
| ABORT-A2 (`slot.plugin()` not a pointer) | Cleared | `vendor/distingNT_API/include/distingnt/slot.h` (`_NT_algorithm*` return) |
| ABORT-A3 (`NT_MULTICHAR` byte order wrong) | Cleared | `api.h:120` — `a` at bits 0-7 |
| ABORT-A4 (combined 6-pilot text > 130 KB) | Deferred to integration | `arm-none-eabi-size` at step 9 |

`_NT_uiData` confirmed: `encoders[2]` (delta -1/0/+1), `controls` (current button bitmask), `lastButtons` (previous snapshot). Edge detection is per-bit `(controls & bit) && !(lastButtons & bit)` (not XOR).

`NT_getSlot(_NT_slot& slot, uint32_t index)` populates by reference and returns `bool` for success. `_NT_slot::plugin()` returns `_NT_algorithm*` (null for built-ins); `_NT_slot::guid()` returns `uint32_t` factory guid.

Multi-factory `.o` pattern (per `examples/multiple.cpp`): single `extern "C" pluginEntry` switches on `kNT_selector_version`, `kNT_selector_numFactories`, `kNT_selector_factoryInfo`. Each factory exposes its own guid.

## Deviation from kickoff step 1.6 (hardware hasCustomUi probe)

The kickoff requires probe-deploying a minimal `hasCustomUi` claim plug-in to confirm hardware honors the claim by suppressing default encoder behavior. This brainstorm defers that probe to the hardware smoke check (step 10) rather than bifurcating into a separate audit deploy. Justification:

- ABORT-A1 (factory-side exposure) is cleared from header inspection.
- The standalone deploy of each pilot in step 10 IS a hardware test of the claim. Every pilot's standalone customUi claims `kNT_encoderL | kNT_encoderButtonL | kNT_button1`; firmware-side honoring is observable directly. If firmware does not honor the claim, the smoke check fails immediately and the refactor halts.
- A dedicated audit probe is duplicate work that does not validate anything the step 10 smoke check does not already validate.

This deviation is documented in the spec footer. It does not weaken the abort guard; it relocates the verification site.

## Architecture (output of the pilot release)

```
build/arm/
├── Compare.o            (≤ 20 KB)   new pilot plug-in
├── ClockDivider.o       (≤ 20 KB)   new pilot plug-in
├── VectorLFO.o          (≤ 20 KB)   new pilot plug-in
├── Cumulus.o            (≤ 20 KB)   new pilot plug-in
├── Relabi.o             (≤ 20 KB)   new pilot plug-in
├── ProbabilityDivider.o (≤ 20 KB)   new pilot plug-in
├── Hemispheres_host.o   (~16 KB)    new host plug-in (2 slots, 64x64 each)
├── Quadrants_host.o     (~16 KB)    new host plug-in (4 slots, 64x64 each)
├── Hemispheres.o        (~82 KB)    bundled — RETAINED for transition window
└── Hemispheres2.o       (~64 KB)    bundled — RETAINED for transition window
```

The function-pointer-in-data interface bridges hosts to per-applet plug-ins through a struct stored on each algorithm instance. The NT loader does not resolve those pointers; they are populated by each applet's `construct()` from its own `.text` (already mapped in ITC via its own registration). Hosts call through the pointers as indirect-into-mapped-text. No cross-`.o` symbol resolution is required.

## Decisions captured (refines kickoff decisions where audit drives adjustment)

| # | Decision | Source |
|---|---|---|
| 1 | Three-release sequence: Pilot (6 + 2 hosts) → Mass-port (50) → Cleanup (delete bundled). | Kickoff §"Release sequence" |
| 2 | Pilot set: Compare, ClockDivider, VectorLFO, Cumulus, Relabi, ProbabilityDivider. | Kickoff §"Pilot applet selection" |
| 3 | Per-applet bus parameters declared in each applet's manifest. No fixed L/R/A/B convention. | Kickoff #3 |
| 4 | Bundled `.o` retained throughout pilot AND mass-port releases. Cleanup release deletes after one-release transition. | Kickoff #4 |
| 5 | After bundled deletion, old presets break. No migration tooling. Transition window is the safeguard. | Kickoff #5 |
| 6 | All 56 applets in scope. ProbLoopLinker singleton is `.o`-private; gotcha documented if ProbabilityMelody is ever ported. | Kickoff #6 |
| 7 | Two host source files (`Hemispheres.cpp`, `Quadrants.cpp`), not a parameterized template. Shared helpers under `shim/include/host_helpers.h`. | Kickoff #7 |
| 8 | NT control claim via `hasCustomUi` + `customUi`. Per-applet standalone: `kNT_encoderL \| kNT_encoderButtonL \| kNT_button1`. Hosts claim per their slot count + control map. | Kickoff #8 |
| 9 | `HemiPluginInterface` has `magic` (`'HMI1'`) + `interface_version` (starts at 1). Hosts validate; mismatches render incompatible stub. | Kickoff #9 |
| 10 | One host per preset is supported. Both-hosts-loaded scenario tested at hardware smoke step; certified or documented as constraint. | Kickoff #10 |
| 11 | Per-applet `draw()` delegates to `render_view(self, 0, 0)`. Returns `true` to suppress firmware parameter strip. | Kickoff #11 |
| 12 | Manifest schema frozen at the schema-freeze checkpoint after pilot integration. Mass-port and later releases use the frozen schema. | Kickoff #12 |
| 13 | Guid prefix `'Hm'` (bytes 0-1). Build-time `static_assert` per manifest. | Kickoff #13 |
| 14 | Quadrants R-encoder routes to `on_encoder_turn_shifted`; defaults to `on_encoder_turn` in `construct()` unless applet overrides. | Kickoff #14 |
| 15 | Directory layout: `plugins/applets/`, `plugins/hosts/`, `plugins/probes/`. CLAUDE.md updated to deprecate top-level `applets/`. | Kickoff #15 |
| 16 (audit) | `VENDOR_DEPS_<applet>` empty for all 6 pilots. Kickoff's lorenz assignment for VectorLFO and Relabi is incorrect; lorenz is unreachable from either include graph. | Audit findings |
| 17 (audit) | Hardware `hasCustomUi` probe deferred to step 10 hardware smoke. Justified in spec footer. | Audit findings |

## Status per pilot applet (pre-implementation)

| Applet | Status | Notes |
|---|---|---|
| Compare | Ready | Zero deps. Smallest baseline. |
| ClockDivider | Ready | Single in-baseline header. |
| VectorLFO | Ready | vec_osc + tideslite all in baseline; `constexpr` path only. |
| Cumulus | Ready | Zero deps. 10x gotcha hits `_per_applet_runtime` step wrapper, not the applet. |
| Relabi | Ready | RelabiManager header-only, vendor-located. Largest text (4204 B). |
| ProbabilityDivider | Ready | HSProbLoopLinker header-only, vendor-located. Singleton private to its `.o`. |

## Exclusions named

- The 50 non-pilot applets are mass-port scope.
- Drum1, Drum2, ASR-Drums and the 3 Phase-6-deferrals (WTVCO, DuoTET, EbbAndLfo) follow their existing audit posture; not pilot scope.
- The cleanup-release deletion of `applets/Hemispheres.cpp` plus `applets/Hemispheres2.cpp` is out of scope for the pilot release.
- Migration tooling for old presets is out of scope (kickoff #5).

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Per-applet `.o` exceeds 20 KB after measurement | Spec gates integration on `arm-none-eabi-size`. Over-budget triggers either (a) reducing standalone surface or (b) escalating the per-plug-in budget if the combined hardware-side ITC still fits. |
| Both-hosts-loaded preset overflows runtime ITC pool | Documented in kickoff #10. Smoke check certifies or documents constraint. PR captures the result. |
| `HemiPluginInterface` magic mismatch at runtime | Hosts validate before every call. Renders incompatible stub. Per-applet construct() populates magic + version; build-time `static_assert` per manifest enforces guid prefix. |
| Implementer subagent dispatches from `main` instead of `dr/per-applet-pilot` | Worktree-dispatch checklist inlined in plan doc. Each worktree branched explicitly from `dr/per-applet-pilot`. Pre-commit hook ancestry check. |
| Implementer subagent stages setup-owned files | Pre-commit hook hard-rejects on `per-applet-applet/*` and `per-applet-host/*` branches for any file outside the per-implementer allowed surface. |
