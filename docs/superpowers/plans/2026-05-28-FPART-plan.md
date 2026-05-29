# FPART (APP_FPART) O_C app port: plan

- Vendor pin: `7800d929`. Issue #41. Branch: `dr/fpart-port`.
- Parallelism: none. This is a single-app sequential port (the #36 track does
  one app per PR). The steps below are ordered by dependency, not fanned out.

## Worklist (ordered)

1. Layer 0 (shared): raise `oc_runtime::kMaxBlobBytes` 256 -> 512 in
   `plugins/apps/_per_app_runtime.h`. Baseline-green check: re-run the existing
   OC app tests (Low_rents, Harrington1200) to confirm the larger blob bound is
   a no-op for them.
2. Shim shadow guard poison: define the vendor include guards `OC_MENUS_H`,
   `OC_APP_H_`, `OC_DAC_H_`, `OC_ADC_H_` in the matching shim shadows. FPART is
   the first app to quote-include those vendor siblings from inside a vendor app
   header (`APP_FPART.h:37-43`). Regression: rebuild the other OC apps (host +
   ARM) and the Hemisphere applet suite.
3. TDD: write `harness/tests/test_oc_app_FPART.cpp` first (factory, draw,
   round-trip, pitch 1V/oct, param sync, offset), then implement to green.
4. Implement `plugins/apps/FPART.cpp` + `shim/include/oc_app_manifests/FPART.h`,
   add `FPART` to `OC_APP_LIST` (empty `VENDOR_DEPS_FPART`). Forward-declare the
   six vendor button helpers (Arduino auto-prototype gap).
5. Build `make build/arm/FPART.o`; confirm `.text` under the cap and the
   unresolved-symbol set is the firmware contract only.
6. Integration: `make test-oc-apps-all`, `make arm`, `make test-applets`.
7. PR off `dr/fpart-port` -> `main`. Hardware smoke check (ADD on device via the
   OCFP GUID) is post-PR.

## DAG note

Steps 1 and 2 are parent-only shared-surface edits (the runtime cap and the four
shim shadows) and must land with a baseline-green check before the per-app code
(steps 3-5) builds on them. There is no Layer 1 fan-out: a single app port has
one implementer unit, so the worklist is sequential by construction.

## Abort budget

- During audit: vendor pin shifted (expect `7800d929`); base branch regression.
  Fired finding: the chord-int int16 range fork; resolved by maintainer decision
  (head-only params). Not an abort.
- During implementation: more than the documented two deviations from the
  Harrington1200 recipe would mean the recipe does not fit; halt and reassess.
  Actual: exactly two (head-only `num_settings`; button-helper forward decls).
- During integration: any baseline regression on the existing OC apps or the
  Hemisphere applet suite from the shared-shadow poison.
- During verification: FPART does not ADD on hardware (post-PR smoke check).

## Status

Steps 1-6 complete and green. Step 7 (PR) pending. Hardware smoke check pending
physical access.
