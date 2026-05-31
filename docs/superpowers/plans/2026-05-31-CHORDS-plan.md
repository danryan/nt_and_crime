# Plan: CHORDS (Chords) NT plug-in

Date: 2026-05-31
Status: implemented
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #51

Single unit, single implementer, on the shared branch `dr/oc-apps-port-2`. No
parallel fan-out. Sequenced (shared shim files: `OC_menus.h`, `OC_strings.h`,
`globals.cpp`).

## Worklist

1. Manifest `CHORDS.h` (GUID `OCCH`; 4 CV in / 4 CV out / 4 trig in).
2. App `CHORDS.cpp` (PASSENCORE template; `ui_events.h` first; skip mask index 3;
   default blob; `dispatch_custom_ui_factory<true>`; test seams).
3. Makefile: `OC_APP_LIST += CHORDS`; `OC_chords.o` + `OC_input_map.o` deps
   (host + ARM).
4. Test `test_oc_app_CHORDS.cpp`. Enumerate first-compile gaps with
   `g++ -ferror-limit=0`; resolve (no vendor edit); get host green.
5. Shim additions: `seq_directions` (OC_strings.h + globals.cpp); `DrawChord` /
   `DrawMiniChord` (OC_menus.h, via `OC_chords.h` include).
6. ARM: `make build/arm/CHORDS.o`; `.text` under cap and `nm` clean.
7. Regression: full OC-app suite green; every OC app ARM-builds (shared
   OC_menus.h touch); one hem applet host + ARM still green (shared globals.cpp).
8. Docs: CHORDS lessons paragraph in CLAUDE.md; `markdownlint` the docs +
   CLAUDE.md.
9. Commit on `dr/oc-apps-port-2`.

## Abort budget

- Step 4: more than 2 unforeseen poison shadows -> halt.
- Step 4: HSIOFrame.h or any vendor edit needed -> halt (confirmed not needed).
- Step 6: `.text` over ~82 KB -> halt.
- Step 7: any other OC app or hem applet regresses -> halt and fix.

## Outcome

Shipped. Host 7/7 (81 assertions). ARM `.text` 34275 B, `nm` clean. Full OC-app
host suite (14 binaries) PASS; all 14 OC apps ARM-build clean; Cumulus hem applet
host (26 assertions) + ARM clean.

No HSIOFrame blocker (CHORDS does not include it). The PASSENCORE template carried
over directly: mid-array U16 mask (index 3) excluded, mask persists through the
default `make_facade(&chords)` blob (it lives in the SettingsBase), no whole-app
override. Two vendor editors (scale + chord), `<true>` long-press dispatch. Shim
additions were three mechanical hand-ports: `seq_directions`, `DrawChord`,
`DrawMiniChord` (the latter two pulling `OC_chords.h` into the shim `OC_menus.h`;
note the OC_chords / OC_chords_presets mutual recursion forces including
`OC_chords.h` first). Documented fidelity limit: `OC::user_chords[]` is not in the
SettingsBase and the vendor blob does not persist it either, so chord-slot edits
do not survive a preset reload (matches vendor).
