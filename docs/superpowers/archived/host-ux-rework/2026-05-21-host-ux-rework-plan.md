# Plan: per-applet host UX rework

Date: 2026-05-21
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-21-host-ux-rework-brainstorm.md`
Spec: `docs/superpowers/specs/2026-05-21-host-ux-rework-design.md`
Branch: `worktree-dr+host-ux-rework`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Parallelization strategy

Work splits into 5 stages. Stages 1 and 4 are gating (hardware on Dan); stages 2/3 are deterministic and can run on the feature branch directly. Stage 3a and 3b are independent host edits sharing only the read-only `host_proxy.h` header and `host_proxy.cpp` implementation, so they can be dispatched as two parallel implementer subagents in worktrees branched from the feature branch (per `~/.claude/rules/parallel-execution.md`).

| Stage | Work | Mode | Gate |
|-------|------|------|------|
| 1 | Build reentrancy probe `.cpp`, host test for probe state machine, compile `make arm` | sequential on feature branch | none |
| 1b | Hardware deploy + observe reentrancy | Dan | end of stage 1 |
| 2 | Implement `shim/include/host_proxy.h` + `shim/src/host_proxy.cpp` + `harness/tests/test_host_proxy.cpp` | sequential on feature branch | stage 1b result |
| 3a | Wire Hemispheres host to `host_proxy::` | parallel implementer worktree | stage 2 merged to feature branch |
| 3b | Wire Quadrants host to `host_proxy::` | parallel implementer worktree | stage 2 merged to feature branch |
| 4 | Integration on feature branch (cherry-pick 3a+3b, regen Makefile entries if needed, `make test-applets` + `make arm`) | sequential | stages 3a and 3b complete |
| 5 | Hardware smoke (Dan) + PR open | Dan | stage 4 green |

End-to-end wallclock approximates: stage 1 + stage 1b wait + stage 2 + max(stage 3a, 3b) + stage 4 + smoke. Stages 3a/3b together is the only parallel slice; they are estimated as the longest single piece (~one session) so parallelization halves it.

## Stage 1: reentrancy probe

### Files (writable surface)

- `plugins/probes/reentrancy_probe.cpp` (new)
- `Makefile` (add `reentrancy_probe` to `BUILD_ARM_PROBES` if such a section exists, or follow the pattern used by `aeabi_probe`)

### Forbidden surface

- Anything under `applets/`, `plugins/applets/`, `plugins/hosts/`, `shim/`, `vendor/`, `harness/`.

### Steps

1. Read existing `plugins/probes/aeabi_probe.cpp` and Makefile probe-build rules to clone the pattern.
2. Implement `reentrancy_probe.cpp` per spec section "Spec for the reentrancy probe":
   - 2 parameters: "A" (min 0, max 1000, def 0), "B" (min 0, max 1000, def 0).
   - Inline counters in `_NT_algorithm` derivative: `pc0_calls`, `pc1_calls`, `pc1_during_pc0`, `pc0_in_flight`.
   - `parameterChanged(self, 0)`: set `pc0_in_flight = true`, increment `pc0_calls`, call `NT_setParameterFromUi(NT_algorithmIndex(self), 1, v[0] + 1)`, set `pc0_in_flight = false`.
   - `parameterChanged(self, 1)`: increment `pc1_calls`; if `pc0_in_flight`, increment `pc1_during_pc0`.
   - `draw()`: render three lines using `NT_intToString` plus `NT_drawText`: `PC0=<n>`, `PC1=<n>`, `NEST=<n>`.
   - `hasCustomUi`: encoder L only. `customUi`: on encoder L direction non-zero, `NT_setParameterFromUi(self, 0, v[0] + 1)`.
3. Build `make arm`; confirm `build/arm/reentrancy_probe.o` exists.
4. (No host unit test — probe is pure firmware; the test of correctness is the hardware observation.)

### Done when

- `make arm` builds `reentrancy_probe.o` with no warnings.
- `arm-none-eabi-nm build/arm/reentrancy_probe.o | grep ' U '` shows only firmware-resolved symbols (`NT_*`, `_GLOBAL_OFFSET_TABLE_`, newlib).

### Commit

`feat(probes): reentrancy_probe diagnostic plug-in for host UX rework`

## Stage 1b: hardware deploy + observe (Dan)

1. `make deploy-sysex SYSEX_PLUGIN=build/arm/reentrancy_probe.o SYSEX_ID=0`.
2. Add to a preset slot. Turn the L encoder one click.
3. Read the screen: record `PC0`, `PC1`, `NEST` values.
4. Edit `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md` "Reentrancy result" section with the observed numbers and interpretation:
   - `NEST > 0` => synchronous reentry; proxy MUST use re-entry guard.
   - `NEST == 0 && PC1 > 0` => deferred (firmware processes pc1 on a later call frame); guard is harmless.
   - `PC1 == 0` => `NT_setParameterFromUi` did not produce a `parameterChanged(1)` at all; halt and post abort report.

### Done when

- Kickoff doc "Reentrancy result" section is populated with observed numbers and interpretation.

## Stage 2: shared proxy aggregator

### Files (writable surface)

- `shim/include/host_proxy.h` (new)
- `shim/src/host_proxy.cpp` (new)
- `harness/tests/test_host_proxy.cpp` (new)
- `Makefile` (add `host_proxy.cpp` to `VENDOR_DEP_ARM_OBJS` and equivalent host-build list; add `test_host_proxy` target)

### Forbidden surface

- Anything under `applets/`, `plugins/`, `vendor/`. (`plugins/hosts/` stays read-only until stage 3.)

### Steps (TDD)

1. RED: write `harness/tests/test_host_proxy.cpp` with the 8 cases listed in spec "Test plan (host unit tests)". Compile-fail expected because `host_proxy::` does not exist yet.
2. Implement `shim/include/host_proxy.h` with the namespace, constants, structs, and function declarations.
3. Implement `shim/src/host_proxy.cpp` with stub bodies returning trivial defaults; confirm test binary links.
4. GREEN: fill in `refresh_enum_strings` until `enum_strings_builds_from_preset_scan` passes.
5. GREEN: fill in `resolve_enum_to_slot` until `enum_resolves_to_slot_index` passes.
6. GREEN: fill in `aggregate_slot` until 3 aggregate tests pass.
7. GREEN: fill in `decode_forward` until 2 decode tests pass.
8. GREEN: implement re-entry guard helper (or inline the bool) until `reentry_guard_drops_recursive_forward` passes.
9. MUTATE: run `mutation-testing` skill on `host_proxy.cpp`; address surviving mutants.
10. `make test-applets` continues to pass (no regression in existing 2596-assertion suite).
11. `make arm` continues to build all targets cleanly.

### Done when

- 8 new test cases pass.
- Mutation testing report reviewed.
- `make test-applets` total assertion count grows by the new cases; existing tests still pass.
- `make arm` clean.

### Commit

`feat(shim): host_proxy aggregator helper for slot enum + parameter proxying`

## Stage 3a: Hemispheres host wiring

### Files (writable surface, implementer worktree)

- `plugins/hosts/Hemispheres_host.cpp`
- `harness/tests/test_hemispheres_host.cpp` (new; covers selector + proxy behavior at the host level using existing `NT_HEM_HOST_SIM` seam)

### Forbidden surface

- `plugins/hosts/Quadrants_host.cpp`
- `shim/`, `vendor/`, `applets/`, `plugins/applets/`, `plugins/probes/`
- Anything in `harness/` other than the new test file.

### Steps

1. Add `host_proxy::State state` to `_HHInstance`; remove the static `s_params[]` array.
2. Update `calculateRequirements_impl`: `req.numParameters = 2 + 16*2 = 34`; SRAM grows by `sizeof(host_proxy::State)`.
3. Update `construct_impl` per spec section "Host plug-in changes" steps 1-6.
4. Add `parameterChanged_impl` per spec; wire into factory.
5. Update `draw_impl` to call `host_proxy::refresh_enum_strings(state)` once per draw; when enum string set changed, issue `NT_updateParameterDefinition` for selector indices and reaggregate proxy lanes.
6. Add host-level unit tests in `harness/tests/test_hemispheres_host.cpp`:
   - Selector enum populated correctly after `construct` given mocked preset scan.
   - Editing selector triggers re-aggregation.
   - Editing a proxy param forwards via `NT_setParameterFromUi`.
   - Re-entry guard prevents recursive forward (mock `NT_setParameterFromUi` calls back into host `parameterChanged`).
7. `make test-applets` and the new test binary pass.
8. `make arm` clean.

### Pre-commit hook

Re-use the existing `.git/hooks/pre-commit` policy. Implementer worktree branch is `phase7-host/hemispheres` (project convention: `<phase-prefix>-port/<slug>` or `<phase>-host/<slug>` for host work).

### Done when

- All new tests pass.
- `make arm` clean.
- Implementer reports worktree path, branch, commit SHA, test output, and any spec deviations.

### Commit

`feat(plugins/hosts): Hemispheres host parameter proxying via host_proxy`

## Stage 3b: Quadrants host wiring

### Files (writable surface, implementer worktree)

- `plugins/hosts/Quadrants_host.cpp`
- `harness/tests/test_quadrants_host.cpp` (new)

### Forbidden surface

- `plugins/hosts/Hemispheres_host.cpp`
- `shim/`, `vendor/`, `applets/`, `plugins/applets/`, `plugins/probes/`
- Anything in `harness/` other than the new test file.

### Steps

Same shape as 3a, with these specifics:

1. `kNumSlotIndexParams = 4`; SRAM budget allows for `State` plus existing `focused_slot_idx` and `cached_slot[4]`.
2. Replace the existing empty `parameterChanged_impl` body (line 198) with the per-spec implementation.
3. Preserve serialise/deserialise of `focused_slot`.
4. Selectors named `"Slot 0"`..`"Slot 3"`.
5. Default enum values: 1, 2, 3, 4.
6. Host-level tests mirror 3a but for 4 slots.

### Done when

- All new tests pass.
- `make arm` clean.
- Implementer reports worktree path, branch, commit SHA, test output, and any spec deviations.

### Commit

`feat(plugins/hosts): Quadrants host parameter proxying via host_proxy`

## Stage 4: integration

1. Cherry-pick (or merge) 3a and 3b commits onto the feature branch.
2. `make test-applets`, `make test-runtime`, `make arm`.
3. Inspect `arm-none-eabi-readelf -W -S build/arm/Hemispheres_host.o | grep -E "^\s*\[\s*[0-9]+\]"` and confirm `.text` stays well under 82KB.
4. Same for `Quadrants_host.o`.

### Done when

- Full host test suite green.
- ARM builds clean.
- No regression in existing applet tests (2596 assertions baseline).

### Commit

`feat: per-applet host UX rework (enum selector + parameter proxying)` (single integration commit; cherry-pick history kept on the feature branch).

## Stage 5: hardware smoke + PR

Dan runs the smoke checks listed in spec section "Hardware smoke checks". On success:

1. `gh pr create --base main` with body summarizing changes, linking spec/plan, and listing the 5 smoke checks as a Test plan checklist.
2. After CI green and Dan completes the smoke checklist on hardware (or reviewer approves), squash-merge per project convention.

## Worktree-dispatch checklist (parent agent, stages 3a/3b)

Before dispatching the two parallel implementer subagents (stage 3a and stage 3b in a single message):

1. Confirm feature branch `worktree-dr+host-ux-rework` HEAD has stage 2 committed.
2. For each implementer:
   - `git worktree add .worktrees/host-ux-rework-3a -b host-ux-rework/3a worktree-dr+host-ux-rework`.
   - `git worktree add .worktrees/host-ux-rework-3b -b host-ux-rework/3b worktree-dr+host-ux-rework`.
3. Verify the spec doc reachable inside each worktree: `test -f .worktrees/host-ux-rework-3a/docs/superpowers/specs/2026-05-21-host-ux-rework-design.md` and same for 3b.
4. Verify submodules initialized in each worktree: `git -C .worktrees/host-ux-rework-3a submodule status` shows both vendor pins checked out (`-` prefix absent on each line). If not, run `git submodule update --init --depth=1 vendor/distingNT_API vendor/O_C-Phazerville` in each.
5. Install or verify the existing `.git/hooks/pre-commit` enforces the writable/forbidden surface for `host-ux-rework/*` branches. If the hook does not cover this branch prefix, extend it before dispatch.

## Subagent-side verification (each implementer)

Each implementer subagent runs at start:

1. `pwd`, `git rev-parse --abbrev-ref HEAD`. Confirm the expected worktree path and branch from the prompt. Mismatch => abort.
2. `test -f docs/superpowers/specs/2026-05-21-host-ux-rework-design.md`. Missing => abort.
3. Before commit, `git diff --cached -- <allowed-surface>` and `git diff --cached -- <forbidden-surface>`. Forbidden must show zero changes.

## Pre-PR quality gate

Before opening the PR:

- Mutation testing on `host_proxy.cpp` (already run in stage 2; re-run if any change).
- Refactoring assessment on host plug-ins.
- `markdownlint` clean on the three new docs.
- `make test-applets`, `make test-runtime`, `make test-buses`, `make test-draw`, `make test-json`, `make test-params`, `make test-loader` all green.
- `make arm` clean for all target `.o` files.
- ARM `.o` sizes recorded in the PR description.
