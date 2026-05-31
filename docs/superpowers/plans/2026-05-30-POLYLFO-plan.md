# POLYLFO port plan

Sequential single-app port (this track is one app per PR; no parallel fan-out).
Spec: `docs/superpowers/specs/2026-05-30-POLYLFO-design.md`.

## Steps (sequential, each leaves the tree green)

1. Layer 0 shim additions (parent worktree, on `worktree-dr+oc-app-polylfo`):
   - `off_on` (OC_strings.h + globals.cpp).
   - `bitmap_indicator_4x8` (OC_bitmaps.h + icons.cpp, bytes from vendor).
   - `digitalReadFast` / `TR4` (OC_gpio.h + globals.cpp accessor).
   Build an existing OC app host test to confirm no regression.
2. Manifest `shim/include/oc_app_manifests/POLYLFO.h`.
3. Makefile: `OC_APP_LIST += POLYLFO` + `VENDOR_DEPS_POLYLFO` + `VENDOR_DEP_HOST_SRCS_POLYLFO`.
4. TDD: write `harness/tests/test_oc_app_POLYLFO.cpp` (T1-T4 from spec) FIRST. Add a
   `test_oc_app_POLYLFO` target wiring if the Makefile pattern rule does not pick it
   up automatically (it should via `test_oc_app_%`).
5. Write `plugins/apps/POLYLFO.cpp` (Harrington1200 template). Iterate first-compile
   symbol errors as additive shim work (deliverable 3/4 placement, OC_options).
6. Green host test: `make build/host/test_oc_app_POLYLFO && ./build/host/test_oc_app_POLYLFO`.
7. ARM build: `make build/arm/POLYLFO.o`; confirm `.text` under cap and
   `arm-none-eabi-nm` shows the frames symbols resolved (`lut_increments_med`,
   `wt_lfo_waveforms` not `U`).
8. Full regression: `make test-applets` (or the OC app suite) green, `make arm` clean.
9. Docs: CLAUDE.md O_C apps section gets a POLYLFO lessons paragraph if any
   non-obvious gotcha surfaced; `markdownlint` the edited `.md` files.
10. Commit (conventional), push, open PR off `main`. Flag the on-device ADD smoke
    check (GUID OCPL) as pending hardware access.

## Abort conditions

- Shim prereq list exceeds ~6 items at first compile -> halt, reassess boundary.
- `frames_poly_lfo.cpp` needs a non-shadowable Teensy header -> halt, report.
- `.text` over ~82 KB cap -> halt (not expected; frames engine is small).
