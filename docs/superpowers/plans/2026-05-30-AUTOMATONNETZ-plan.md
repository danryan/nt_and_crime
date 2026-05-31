# AUTOMATONNETZ port plan

Single-app port on the shared `dr/oc-apps-port` branch (one branch + one PR for
all remaining #36 apps; no per-app stacking). Spec:
`docs/superpowers/specs/2026-05-30-AUTOMATONNETZ-design.md`.

## Steps (each leaves the tree green)

1. Layer 0: shim shadow `shim/include/util/util_sync.h` (poison `UTIL_SYNC_H_`,
   portable non-atomic `CriticalSection`/`TryLock`/`Lock`). Sanity: existing OC
   app host tests still green (no regression; nothing else includes it yet).
2. Manifest `shim/include/oc_app_manifests/AUTOMATONNETZ.h` (GUID OCAN).
3. Makefile: `OC_APP_LIST += AUTOMATONNETZ`; `VENDOR_DEPS_AUTOMATONNETZ :=`
   (empty).
4. TDD: `harness/tests/test_oc_app_AUTOMATONNETZ.cpp` first, then
   `plugins/apps/AUTOMATONNETZ.cpp` (Harrington1200 template; `#define
   ENABLE_APP_AUTOMATONNETZ`; `dispatch_custom_ui_factory<true>`;
   `construct(..., GRID_SETTING_LAST)`). Iterate first-compile symbol errors as
   additive shim work; enumerate the full set with `g++ -ferror-limit=0
   -fsyntax-only` if more than one surfaces.
5. Green host test; all OC app host tests green.
6. ARM: `make build/arm/AUTOMATONNETZ.o`, `.text` under cap, no unresolved
   `tonnetz::`/`CellGrid` symbols (`arm-none-eabi-nm`); `make arm` clean.
7. Full applet suite green. CLAUDE.md AUTOMATONNETZ lesson + markdownlint.
8. Commit (conventional) on `dr/oc-apps-port`. On-device ADD smoke check (GUID
   OCAN) pending hardware.

## Abort conditions

- Shim prereq list far exceeds the forecast single util_sync shadow plus a few
  additive strings (signals a mis-drawn boundary) -> halt and reassess.
- `util_grid.h` or tonnetz needs a non-shadowable Teensy header on host -> halt.
- `.text` over ~82 KB -> halt (not expected; H1200 is 11-15 KB, AUTOMATONNETZ
  adds the grid walker, expect ~20 KB).
