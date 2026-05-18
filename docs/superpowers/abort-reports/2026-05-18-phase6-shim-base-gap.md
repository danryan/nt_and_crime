# Abort report: Phase 6 shim base class gap

Date: 2026-05-18
Phase: 6 (parallel implementer fan-out for 25 Hemisphere applet ports)
Status: ABORTED at Layer 0 (parent integration discovery)
Branch: `dr/phase6-applets-plan` at commits `1909103` (docs) and `63c5a91` (markers)

## Summary

Phase 6 reached Layer 0 audit, brainstorm, spec, plan, section markers, and pre-commit hook. Layer 0 attempted to register 25 in-scope applets in `HemispheresFactory.h` so Layer 1 implementers could test via `setup_applet(kAppletXxx)` against the existing factory pattern. Registration triggered ~20 compile errors revealing that almost every Phase 6 applet uses vendor `HemisphereApplet` base class members and `HS::`/`OC::Strings::` namespace symbols that the shim base does NOT expose. Phase 5's shim base is materially smaller than vendor's. The gap is broad enough to qualify as a Phase 5b-equivalent decision rather than Layer 2 integration touchpoints.

This report inventories the gap, lists what landed, and recommends a Phase 7 scope to close it.

## What landed on `dr/phase6-applets-plan`

- Commit `1909103`: brainstorm + spec + plan documents at `docs/superpowers/{brainstorms,specs,plans}/2026-05-18-phase6-applets-*.md`.
- Commit `63c5a91`: section markers for 25 Phase 6 in-scope applets in `harness/tests/test_hemispheres.cpp` (added 4 Class C markers; the other 21 were already placed by Phase 5 as forward-looking placeholders) and in `harness/tests/applet_test_helpers.{h,cpp}` (24 empty marker pairs, all in-scope minus Metronome which returns 0 from `OnDataRequest`).
- Pre-commit hook at `.git/hooks/pre-commit`: Phase 6 + Phase 5 template active on `dr/phase6-applets-plan`, `phase6-port/*`, `dr/phase5-deps-plan`, and `phase5-dep/*` branches. Hard-rejects implementer commits that stage shim infrastructure or Phase 5 dep files. No-op on other branches.
- Baseline preserved: `make test-applets` 157 cases / 1816 assertions; `make test-deps` 60 cases / 8564 assertions; `make arm` 0 errors + 3 accepted warnings.

## What did NOT land

- No applet registered in `applet_indices.h` or `HemispheresFactory.h` factory table.
- No PhzIcons or top-level `*_ICON` additions to shim. The audit identified them but never landed since registration was abandoned.
- No shim base class extensions. The audit found them necessary but they are out of Phase 6 scope.
- No Layer 1 implementer fan-out.

The Phase 6 PR is NOT opened. The feature branch contains only docs and section markers.

## The gap (per-applet missing shim surface)

Probing each Phase 6 in-scope applet against the Phase 5 shim by adding it to `HemispheresFactory.h` revealed the following missing shim base / namespace members. All citations refer to `vendor/O_C-Phazerville/software/src/` at SHA `7800d929`.

| Applet | Missing shim base / namespace surface | Vendor source |
|---|---|---|
| VectorLFO | `SemitoneIn(int)`, `CancelEdit()`, `gfxPrintFreqFromPitch(int)`, `gfxSpicyCursor(...)`, `gfxCursor(x,y,w,str,extra)` 5-arg overload | `HemisphereApplet.h:169, HSApplication.h:256, HSUtils.h, HemisphereApplet.h` |
| VectorEG | `SemitoneIn`, `CancelEdit`, `gfxSpicyCursor`, 5-arg `gfxCursor` | same |
| VectorMod / VectorMorph | likely subset of above (not probed beyond fatal-error limit) | same |
| Relabi | `InF(int)` float-CV input, `graphics.printf(...)` (method on shim Graphics class) | `HemisphereApplet.h, hem_graphics.h` |
| LowerRenz | not probed (build hit fatal-error limit before reaching it) | unknown |
| Combin8 | `IndexedInput(...)`, `EditSelectedInputMap(int)`, `PackPackables(...)` | `HSUtils.h, HSApplication.h:256, HSIOFrame.h` |
| Pigeons | `HS::qview`, `HS::PokePopup`, `CancelEdit`, `gfxSpicyCursor` | `HSUtils.h, HSApplication.h, HemisphereApplet.h` |
| Strum | `SemitoneIn`, `OC::Strings::note_names_unpadded` | `HemisphereApplet.h, OC_strings.h` |
| Metronome | `SemitoneIn`, `NOTE4_ICON` | `HemisphereApplet.h, HSicons.h` |
| (remaining Class B + C applets) | not probed before fatal-error limit | likely overlapping subset |

The missing surface clusters into four categories:

1. **Base class methods** that mirror simple vendor members: `SemitoneIn(int ch)` (returns `In(ch) / 128` per vendor `HemisphereApplet.h:169`), `InF(int ch)` (returns `In(ch) / 1536.0f` per vendor pattern), `CancelEdit()` (exits edit mode), `gfxDisplayInputMapEditor()` (CV map UI; can be a stub for host tests), `gfxPrintFreqFromPitch(int)` (prints frequency string), `gfxSpicyCursor(...)` (animated cursor variant), 5-arg `gfxCursor` overload. Each is small in isolation; ten or more applets pull at least one. Aggregate addition is on the order of 50-100 lines on shim `HemisphereApplet.h`.

2. **UI state in HS::** namespace: `HS::qview` (quantizer popup view bool), `HS::PokePopup` type. These are used inside Controller branches in some applets (Pigeons line 93-94). Stubbing as constants/empty types covers the surface but may alter Controller behavior.

3. **OC::Strings extensions**: `note_names_unpadded` array (12 strings for chromatic note names). Trivially addable to shim `OC_strings.h`.

4. **Helpers for UI-driven data packing**: `IndexedInput(...)`, `EditSelectedInputMap(int)`, `PackPackables(...)`. Used by Combin8 in `View()` and `OnDataRequest`. `PackPackables` is critical because it affects `OnDataRequest` bit layout; stubbing wrong breaks round-trip tests. Real implementation mirrors vendor `HSIOFrame.h`.

5. **Graphics class extensions**: `graphics.printf(...)` (formatted print on the shim's `shim::Graphics` class). Used by Relabi `View()`. Stub for host tests is sufficient since View is not exercised by assertions.

6. **Icon stubs**: ~16 PhzIcons + ~13 top-level `*_ICON` (NOTE4_ICON, METRO_L_ICON, etc.). Standard Layer 2 work, would have shipped easily.

## Why this exceeds Phase 6 scope

The Phase 6 kickoff prompt at `docs/superpowers/prompts/2026-05-18-phase6.md` says:

- "This is an applet port phase, not a shim infrastructure phase. Layer 0 work is bounded: section markers + pack helper skeletons + pre-commit hook update + Phase 5 baseline verification. No new shim subsystems land; the recipes in this prompt assume Phase 5's shipped surface."
- "Layer 2: integration on `dr/phase6-applets-plan`. Owns `applet_indices.h`, `HemispheresFactory.h`, `PhzIcons.h` edits + any vendor macro shims (unlikely; Phase 5 deps shipped them)."

The audit reveals that Phase 5 deps did NOT ship the base class surface, the HS:: UI helpers, the OC::Strings additions, the Graphics printf, or the PackPackables / IndexedInput / EditSelectedInputMap helpers. These are not "vendor macro shims" (the kickoff's allowed Layer 2 scope); they are vendor member functions and namespace state from `HemisphereApplet.h`, `HSApplication.h`, `HSUtils.h`, `HSIOFrame.h`, and `hem_graphics.h`.

Adding them is shim infrastructure work, not applet port work. The kickoff's abort budget:

> "Any candidate audit reveals a Phase 5 dep surface gap that would require dep modification (Phase 5 dep surfaces are frozen; this is a Phase 5b decision, not Phase 6)."

The shim base class is not strictly a "Phase 5 dep" (deps live under `shim/include/<area>/`), but the spirit of the rule applies: Phase 6 was scoped as applet ports against a frozen shim. The shim turns out to be insufficient. Extending it is a separate, larger task.

Per the kickoff's class demotion budgets, demoting Combin8 alone (the most obviously gap-affected Class A applet) brings Class A demotions to 3 of 9 (WTVCO, EbbAndLfo, Combin8). At the threshold. Probing reveals that ~10 more applets across all classes need similar shim base extensions. Salvaging Phase 6 by demoting every gap-affected applet would leave fewer than 18 applets in scope, tripping the kickoff's hard halt:

> "Total Phase 6 in-scope count drops below 18 applets after audit + brainstorm."

## Why the audit missed this

The preflight audit (this conversation, earlier turns) probed each candidate with `clang++ -fsyntax-only` against `shim/include/HemisphereApplet.h` directly, including no factory machinery. That probe pulls only the headers needed for the applet class declaration. Method-body symbol lookup is deferred to actual instantiation in a real translation unit.

Adding the applet to `HemispheresFactory.h` and building Hemispheres.cpp triggers method-body parsing and exposes the gaps. The audit shape was wrong: a `-fsyntax-only` probe of an applet header is not equivalent to a full ARM/host build that instantiates the class through the factory.

Recommendation for future audit: use a probe that compiles a small `Applet.cpp` TU which instantiates the applet and calls `Start()` and `Controller()`. This forces method-body parsing and surfaces missing base class members at audit time, not at integration time.

## Phase 7 scope recommendation

The natural Phase 7 is a SHIM BASE EXTENSION phase, paralleling Phase 5's structure (vendor dep ports) but targeting the shim's `HemisphereApplet` base, `HSUtils.h` HS:: namespace, `OC_strings.h`, and `hem_graphics.h`. Per-extension implementer tasks:

- **ext-base-cv**: `SemitoneIn`, `InF`, base-class data-read helpers.
- **ext-base-ui**: `CancelEdit`, `gfxDisplayInputMapEditor`, `gfxSpicyCursor`, `gfxPrintFreqFromPitch`, 5-arg `gfxCursor` overload.
- **ext-hs-ui-state**: `HS::qview`, `HS::PokePopup`, related UI globals.
- **ext-oc-strings**: `OC::Strings::note_names_unpadded` (+ any other missing arrays).
- **ext-graphics-printf**: `shim::Graphics::printf(...)` formatted print.
- **ext-pack-packables**: `PackPackables(...)` and related `HSIOFrame` helpers.
- **ext-input-map-ui**: `IndexedInput(...)`, `EditSelectedInputMap(int)`.
- **ext-icons**: Phase 6 PhzIcons (~16) + top-level `*_ICON` (~14) batched.

Each implementer ships one shim file region + a small targeted test. After Phase 7 lands, Phase 6 (or Phase 8) reopens the applet port batch and Layer 1 dispatches the 25 implementer tasks against the now-sufficient shim.

The Phase 6 docs (brainstorm, spec, plan) on `dr/phase6-applets-plan` are valid templates for the re-attempt. Class C applets (ResetClock, Shuffle, Xfader, Scope) and Class D (Metronome) still inherit the helper-using and clock-mgr recipes from Phase 5 — these don't change in Phase 7.

## Suggested next action for the user

1. Read this report.
2. Decide whether to kick off Phase 7 (shim base extension) immediately, or close Phase 6 with the docs + markers landed as preparation work.
3. If Phase 7: prompt should mirror the Phase 5 kickoff structure (dep-style implementer tasks against a shipped base shim). Vendor SHA stays at `7800d929`.
4. If closing Phase 6: open a PR on `dr/phase6-applets-plan` with this abort report + the docs + the section markers + the pre-commit hook update. The PR captures the audit work and sets up Phase 7.

## Reporting checklist (per kickoff `Reporting` section)

1. Documents produced:
   - `docs/superpowers/brainstorms/2026-05-18-phase6-applets-brainstorm.md`
   - `docs/superpowers/specs/2026-05-18-phase6-applets-design.md`
   - `docs/superpowers/plans/2026-05-18-phase6-applets-plan.md`
   - This abort report.
2. Phase 6 scope: 25 applets planned (A: 7, B: 13, C: 4, D: 1). 0 landed.
3. Layer 0 commit: `63c5a91` (markers). Pre-commit hook updated.
4. Layer 0.5 not executed (skeletons would land here; deferred).
5. Layer 1 not dispatched.
6. Layer 2 not executed.
7. Layer 3 final verification: baseline preserved (157 / 1816 applets, 60 / 8564 deps, arm 0/3).
8. No PR opened.
9. Phase 7+ carry-forward inventory: full Phase 6 applet pool plus the shim base extension itemization above.
