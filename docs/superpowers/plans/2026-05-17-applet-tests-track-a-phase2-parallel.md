# Phase 2 Parallel Finish Plan

Cover the final 4 applets in parallel: ClkToGate, GateDelay, TLNeuron, Cumulus. Each agent works in an isolated worktree off the current branch head, commits its three-file change, and reports back. Orchestrator cherry-picks the four commits sequentially. Wallclock target: time of the slowest single applet (Cumulus, due to the 2-bit gap and 5 cases).

## Why parallel here

Tasks 9-12 are mutually independent:

- Each adds one `pack_<applet>` to `applet_test_helpers.{h,cpp}` (append-only).
- Each adds one per-applet setter helper and a TEST_CASE block to `test_hemispheres.cpp` (append-only).
- No cross-task references. No shared helper changes.

The only conflict surface is the trailing-line region of the three shared files. Git's 3-way merge resolves append-only additions reliably when the appends are at distinct positions in the new content, which they will be once each agent appends to the current end of file.

## Execution

1. Dispatch four implementer agents in a single orchestrator turn (one Agent tool call per applet, all in the same message). Each gets `isolation: "worktree"` so it works on its own copy.
2. Each implementer follows its task description from `docs/superpowers/plans/2026-05-17-applet-tests-track-a-phase2.md`:
   - Task 9: ClkToGate (lines 1160-1338).
   - Task 10: GateDelay (lines 1339-1449).
   - Task 11: TLNeuron (lines 1450-1612).
   - Task 12: Cumulus (lines 1613-1774).
3. Each implementer commits with the conventional commit message stated in the task and reports back its worktree path, branch name, commit SHA.
4. Orchestrator cherry-picks the four commits onto `worktree-applet-tests-phase2-plan` in order: ClkToGate -> GateDelay -> TLNeuron -> Cumulus. Resolve any append-region conflicts by accepting both sides (additive content from each helper file and from `test_hemispheres.cpp`).
5. After all four cherry-picks land, run `make test` to confirm the combined state is green.
6. Dispatch four review pairs in parallel (one combined spec + quality review per applet). Each reviewer reads the relevant commit and reports approval or required fixes.
7. If any reviewer finds blockers, dispatch a follow-up implementer to fix in place on the integrated branch.

## Worktree-conflict expectations

The three files have three deterministic append regions:

- `applet_test_helpers.h`: end of file (after `pack_compare` decl). Each agent adds one decl + comment.
- `applet_test_helpers.cpp`: end of file. Each agent adds one definition.
- `test_hemispheres.cpp`: end of file. Each agent adds `using hem_shim::kApplet<Name>;`, one helper in the anon namespace, and a TEST_CASE block.

Sequential cherry-picks rebuild the file by stacking appends. If a conflict surfaces, the resolution is mechanical: keep both blocks, preserve the order chosen by the orchestrator (ClkToGate, GateDelay, TLNeuron, Cumulus). The `using` declarations at the top of `test_hemispheres.cpp` will need light reconciliation if more than one agent picks the same insertion point; resolve by alphabetical order under the existing block.

## After all four merge

Move to Task 13 (final verification) from the original Phase 2 plan: `make test-applets`, full suite, tagged subsets per applet, hardware sanity gate.

## Out of scope

- Right-side helper coverage (Phase 2.5).
- Splitting `test_hemispheres.cpp` per applet (revisit when over ~2000 lines).
