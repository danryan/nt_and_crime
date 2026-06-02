# Epic #72 Batch 2 plan: MiniSeq cluster (Seq32, SeqPlay7, SwitchSeq)

Spec: `docs/superpowers/specs/2026-06-02-epic72-batch2-miniseq-design.md`.
Audit: #71. Epic: #72. Vendor pin `7800d929`.

## DAG

- Layer 0 (parent, done): `shim/include/OC_patterns.h` + guarded
  `OC::user_patterns` storage in `globals.cpp`, committed on
  `dr/epic72-batch2`. Baseline-green verified (ClockDivider test still passes).
- Layer 1 (parallel): 3 implementer subagents, one per applet, each in an
  isolated worktree branched from `dr/epic72-batch2` (AFTER the Layer-0 commit,
  so the shim dep is present). Each authors manifest + `.cpp` + test, builds host
  test green, builds ARM `.o` clean.
- Layer 2 (integration, parent): copy each implementer's 3 own files;
  add the 3 to `ALL_APPLET_LIST` + `VENDOR_DEPS` in one Makefile commit; union
  any additive shim icon work (dedup); `make test-applets` + ARM build all.
- Layer 3 (verification): full host suite green; 3 ARM `.o` clean nm surface.
  Hardware ADD smoke check post-PR.

## Worktree-dispatch checklist

- Base branch `dr/epic72-batch2` (NOT main), AFTER the Layer-0 commit.
- `git submodule update --init --depth=1 vendor/O_C-Phazerville vendor/distingNT_API`
  in each new worktree; verify the applet header exists.
- Allowed surface per implementer: `plugins/applets/<A>.cpp`,
  `shim/include/applet_manifests/<A>.h`, `harness/tests/test_applet_<A>.cpp`,
  and (only on a missing-icon compile error) `shim/include/PhzIcons.h` +
  `shim/src/icons.cpp` + `shim/include/HSicons.h`. Implementers may add a
  temporary Makefile entry to build locally; the orchestrator owns the real
  Makefile edit and copies only the allowlisted files at integration.

## Abort budget

- Layer 1: more than 1 of 3 implementers aborts substantively -> halt, reassess
  Layer 0 (the shim dep is wrong or incomplete). A missing-icon stub is not an
  abort.
- Integration: `make test-applets` regression on a shipped applet -> halt.
- Verification: any unexpected unresolved ARM symbol -> fix the one applet; more
  than 1 needing it -> halt.

## Parallelization

The 3 applets are independent (disjoint own files; shared Makefile + icon files
deferred to integration). Parallel fan-out, slowest-single-applet wallclock.
