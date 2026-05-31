# Plan: PASSENCORE NT plug-in

Date: 2026-05-31
Status: accepted
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #47

Single unit, single implementer, on the shared branch `dr/oc-apps-port-2`. No
parallel fan-out (one app). Sequenced because the steps share files (the app
`.cpp`, the Makefile, the shim `OC_scales.cpp`).

## Worklist

1. Shim: port the full vendor 99-entry `scale_names` table into
   `shim/src/quant/OC_scales.cpp` (replace the `{nullptr}` stub).
2. Manifest: create `shim/include/oc_app_manifests/PASSENCORE.h` (GUID `OCPS`).
3. App: create `plugins/apps/PASSENCORE.cpp` per the spec recipe (subset facade,
   `dispatch_custom_ui_factory<false>`, test seams).
4. Makefile: add `PASSENCORE` to `OC_APP_LIST`; set `VENDOR_DEPS_PASSENCORE` and
   `VENDOR_DEP_HOST_SRCS_PASSENCORE` to the OC_chords + OC_input_map objects.
5. Test: create `harness/tests/test_oc_app_PASSENCORE.cpp` per the spec test
   plan. Build `build/host/test_oc_app_PASSENCORE`, resolve first-compile shim
   gaps as additive shim work, get green.
6. ARM: `make build/arm/PASSENCORE.o`; confirm `.text` under the cap and
   `arm-none-eabi-nm` shows no unresolved vendor symbols.
7. Regression: `make test-oc-apps-all` stays green (the scale_names change
   touches a shared shim file; confirm no other OC app regresses).
8. Docs: add a PASSENCORE lessons paragraph to CLAUDE.md; `markdownlint` the
   three workflow docs and CLAUDE.md.
9. Commit on `dr/oc-apps-port-2` (conventional commit, scope `apps`).

## Worktree-dispatch checklist

Not applicable: single implementer in the current worktree on
`dr/oc-apps-port-2`. Submodules already initialized. No subagent fan-out.

## Abort budget

- Step 5: more than 2 unforeseen poison shadows -> halt, reassess boundary.
- Step 6: `.text` over ~82 KB -> halt (split needed; not expected).
- Step 7: any other OC app regresses from the scale_names change -> halt and fix
  before proceeding.
