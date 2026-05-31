# ENVGEN port plan

Sequential single-app port (one app per PR; stacked on the POLYLFO branch since
the OC::App track carries shim/runtime learnings forward). Spec:
`docs/superpowers/specs/2026-05-30-ENVGEN-design.md`.

## Steps (each leaves the tree green)

1. Raise `kMaxSettings` 80 -> 160 (`_per_app_runtime.h`). Verify an existing app's
   `.text` is unchanged (it is: H1200 27344, POLYLFO 29674 both before/after).
2. Layer 0 shim: `menu::DrawMask` (both overloads) into `OC_menus.h`;
   `menu::SettingsListItem::DrawValueMax`; `OC::DigitalInputDisplay` into
   `OC_digital_inputs.h` (include `OC_config.h` for `OC_CORE_ISR_FREQ`);
   `OC_CORE_TIMER_RATE` into `OC_config.h`; `bitmap_loop_markers_8` /
   `kBitmapLoopMarkerW` into `OC_bitmaps.h` + `menus.cpp`; four `OC::Strings`
   tables into `OC_strings.h` + `globals.cpp`.
3. Manifest `oc_app_manifests/ENVGEN.h` (GUID OCEG).
4. Makefile: `OC_APP_LIST += ENVGEN` + `VENDOR_DEPS_ENVGEN`
   (peaks_multistage_envelope + peaks_resources).
5. TDD: `harness/tests/test_oc_app_ENVGEN.cpp` first, then `plugins/apps/ENVGEN.cpp`
   (BBGEN quad template; `#define ENABLE_APP_PIQUED`). Iterate first-compile
   symbol errors as additive shim work.
6. Green host test; all OC app host tests green (no regression from kMaxSettings).
7. ARM: `make build/arm/ENVGEN.o`, `.text` under cap, peaks symbols resolved;
   `make arm` clean.
8. Full applet suite green. CLAUDE.md ENVGEN lessons + markdownlint.
9. Commit (conventional), push, PR (base = POLYLFO branch). On-device ADD smoke
   check (GUID OCEG) pending hardware; note the kMaxSettings reboot-to-recache.

## Abort conditions

- Shim prereq list far exceeds the forecast (signals a mis-drawn boundary). ENVGEN
  surfaced 5 net-new symbols beyond DrawMask/strings; all are legitimate
  feature-rich-app needs and mechanical, so proceed (not a boundary defect).
- `peaks_multistage_envelope.cpp` needs a non-shadowable Teensy header -> halt.
- `.text` over ~82 KB -> halt (not expected; ENVGEN.o is ~35 KB).
