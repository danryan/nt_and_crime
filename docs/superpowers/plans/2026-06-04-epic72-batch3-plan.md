# Epic #72 Batch 3 plan: ASR

Spec: `docs/superpowers/specs/2026-06-04-epic72-batch3-asr-design.md`.
Audit: #71. Epic: #72. Vendor pin `7800d929`.

## DAG

- Layer 0 (shipped on the batch-1 branch, inherited here): the opt-in
  static-arena `operator new` in `shim/src/cxx_runtime_stubs.cpp`. This batch
  branches from `dr/epic72-batch1` so the arena is present; the batch-3 PR
  merges after #73.
- Layer 1 (single applet): ASR ported as build token `ASRHemi` (manifest +
  `.cpp` + test), opting into the arena via `#define NT_HEM_NEED_HEAP_ARENA 1`.
- Layer 2 (verification): full `make test-applets` green; `ASRHemi.o` ARM clean
  with `operator new` resolved to the arena and only the firmware-contract
  unresolved surface. Hardware ADD smoke check post-PR (the arena makes the
  RingBufferManager singleton allocation succeed instead of faulting).

## Single-applet execution

ASR is one unit, so no parallel fan-out. One implementer worked directly on the
feature branch. The only shared-surface additions are `PhzIcons::ASR` (icon
stub) and the Makefile `ALL_APPLET_LIST` / `VENDOR_DEPS_ASRHemi :=` entries,
both authored in place.

## Abort budget

- Build: `ASRHemi.o` `operator new` resolving to the nullptr stub (not the
  arena) means the `NT_HEM_NEED_HEAP_ARENA` define is missing -> fix before
  ship. Any quantizer/braids symbol unresolved -> add a `VENDOR_DEPS` link.
- Verification: full host-suite regression -> halt.

## Stacking note

Because batch 3 branches from `dr/epic72-batch1`, the batch-3 PR diff includes
the batch-1 commits until #73 merges. Merge order: #73 then the batch-3 PR.
