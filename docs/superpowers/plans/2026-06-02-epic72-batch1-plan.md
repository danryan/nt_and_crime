# Epic #72 Batch 1 plan: EASY no-shim mass-port (12 applets)

Spec: `docs/superpowers/specs/2026-06-02-epic72-batch1-easy-massport-design.md`.
Audit: #71. Epic: #72. Vendor pin `7800d929`.

## Parallelization strategy

The 12 applets are fully independent: each touches its own
`plugins/applets/<A>.cpp`, `shim/include/applet_manifests/<A>.h`, and
`harness/tests/test_applet_<A>.cpp` (disjoint new files), plus two append-region
edits to the shared `Makefile` (`ALL_APPLET_LIST` and the `VENDOR_DEPS_*`
block). No cross-applet data or ordering dependency. This is the established
mass-port shape (49 applets shipped this way).

Execution: parallel fan-out, one implementer subagent per applet, each in an
isolated worktree branched from `dr/epic72-batch1`. The shared `Makefile` edits
are deferred to integration: implementers do NOT edit the Makefile in their
worktree (it is the integration-owned shared surface); the orchestrator adds
all 12 to `ALL_APPLET_LIST` + the `VENDOR_DEPS_*` block in one integration
commit. This keeps implementer worktrees conflict-free.

End-to-end wallclock target: slowest single applet port plus integration, not
the sum of 12.

## Layer structure (DAG)

- Layer 0 (parent, done): feature branch `dr/epic72-batch1` from `origin/main`,
  submodules initialized, spec + plan committed.
- Layer 1 (parallel): 12 implementer subagents. Each authors its three new
  files from the recipe + named vendor source, builds its host test green,
  builds its ARM `.o` clean, commits ONLY its three files on its own branch.
- Layer 2 (integration, parent): cherry-pick / copy each implementer's three
  files onto `dr/epic72-batch1`; add all 12 to the Makefile in one commit;
  run `make test-applets` and `make arm` for the full set.
- Layer 3 (verification): full `make test-applets` green; every `build/arm/<A>.o`
  builds and `nm` shows only the resolved firmware surface. Hardware ADD smoke
  check after PR open (needs physical NT).

## Worktree-dispatch checklist (orchestrator)

For each implementer:

- Base branch is `dr/epic72-batch1` (the feature branch), NOT `main`.
  `git worktree add .worktrees/b1-<A> -b dr/b1-<A> dr/epic72-batch1`.
- Run `git submodule update --init --recursive --depth=1` in the new worktree
  before dispatch (worktrees do not inherit submodule state); verify
  `vendor/O_C-Phazerville/software/src/applets/<A>.h` exists.
- Allowed surface for the implementer: exactly
  `plugins/applets/<A>.cpp`, `shim/include/applet_manifests/<A>.h`,
  `harness/tests/test_applet_<A>.cpp`, and (only if a missing-icon compile
  error forces it) `shim/include/PhzIcons.h` + `shim/src/icons.cpp`.
  Forbidden: `Makefile`, `_per_applet_runtime.h`, any other applet's files,
  any shim core file other than the icon files above.
- The implementer cannot build its applet `.o` through the Makefile without its
  ALL_APPLET_LIST entry, so the implementer adds a TEMPORARY local Makefile
  entry to build/test, then reverts it before commit (commit must not stage
  Makefile). Alternatively the implementer commits the three files and reports
  the build was verified via a temporary entry. The orchestrator owns the real
  Makefile edit.

## Abort budget

- During Layer 1 dispatch: more than 3 of 12 implementers abort substantively
  -> halt, reassess the recipe (the audit said all 12 are EASY; >3 aborts means
  the EASY classification or the recipe is wrong). An implementer needing a new
  icon stub is NOT an abort (expected additive work).
- During integration: any Makefile merge conflict beyond the two append regions
  -> halt. `make test-applets` regression on a previously-shipped applet ->
  halt (the shared Makefile/runtime was wrongly touched).
- During verification: any `build/arm/<A>.o` with an unexpected unresolved
  symbol -> fix the missing dep link or shim gap for that one applet; if more
  than 3 need it, halt and reassess.

## Integration sequence

1. Collect the three files from each implementer worktree onto
   `dr/epic72-batch1`.
2. One Makefile commit: 12 names appended to `ALL_APPLET_LIST`, 12
   `VENDOR_DEPS_<A> :=` lines appended to the block.
3. `make test-applets` (full set) green.
4. `make arm` builds all `.o` including the 12 new ones.
5. Open PR from `dr/epic72-batch1` to `main`; check off the #72 Batch 1 boxes;
   note the hardware ADD smoke check as the post-merge follow-up.
