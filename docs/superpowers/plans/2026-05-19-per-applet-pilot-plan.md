# Plan: Per-applet refactor pilot release

Date: 2026-05-19
Status: Active
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-19-per-applet-pilot-brainstorm.md`
Spec: `docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md`
Branch: `dr/per-applet-pilot`
Worktree: `.worktrees/per-applet-pilot`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Dependency declaration

Per `~/.claude/rules/parallel-execution.md`: the 6 pilot per-applet ports are independent at the file surface. Each owns its dedicated `plugins/applets/<APPLET>.cpp`, `shim/include/applet_manifests/<APPLET>.h`, and `harness/tests/test_applet_<APPLET>.cpp`. They share no state and share no types beyond the setup-committed `HemiPluginInterface`, `applet_manifest`, and `_per_applet_runtime` headers. Their commits are non-overlapping.

The 2 host implementations are also independent at the file surface (`plugins/hosts/Hemispheres.cpp` vs `plugins/hosts/Quadrants.cpp`, separate test files). They share `host_helpers` already committed by Layer 0.

Layer 1a (6 pilot implementers) dispatches in a SINGLE message with 6 Agent calls. Layer 1b (2 host implementers) dispatches in a SINGLE message with 2 Agent calls AFTER Layer 1a commits land (hosts depend on at least one pilot existing as a compilable `.o` so the host tests have a real target to instantiate against).

End-to-end wallclock target: Layer 0 setup commits + slowest single pilot (Relabi, 4204 B unique text) + integration + Layer 1b host implementations + hardware smoke + PR.

## Worktree-dispatch checklist invocation

The parent agent honors the checklist in `~/.claude/rules/parallel-execution.md:59` for every implementer dispatch in Layer 1a and Layer 1b:

1. **Base branch explicit**: each implementer worktree is provisioned with `git worktree add .worktrees/per-applet-applet-<slug> -b per-applet-applet/<slug> dr/per-applet-pilot` (and the corresponding `per-applet-host/<slug>` shape for hosts). The base is the feature branch, NOT `main`.
2. **Spec docs reachable**: parent verifies `test -f .worktrees/per-applet-applet-<slug>/docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md` before subagent launch.
3. **Submodules initialized in new worktree**: parent runs `git -C .worktrees/per-applet-applet-<slug> submodule update --init --recursive --depth=1` after `git worktree add`.
4. **Constrain writable surface**: implementer prompt names exact files. Pre-commit hook (in `.git/hooks/pre-commit`, shared by all worktrees) hard-rejects commits outside the allowed surface for the implementer's branch.

This checklist runs once per implementer dispatched (6 + 2 = 8 times total, scripted).

## Pre-commit hook content (Layer 0 deliverable)

Layer 0 updates `.git/hooks/pre-commit` to the per-applet-pilot template. The hook is a no-op on branches it does not match (existing Phase 5/Phase 6 patterns continue to work); it enforces the implementer contract on `per-applet-applet/*` and `per-applet-host/*`, and the integration contract on `dr/per-applet-pilot`.

```sh
#!/bin/sh
# Per-applet pilot pre-commit hook. Active on dr/per-applet-pilot,
# per-applet-applet/*, per-applet-host/* branches; preserves existing
# Phase 5/Phase 6 patterns; no-op elsewhere.

set -e
branch=$(git rev-parse --abbrev-ref HEAD)

case "$branch" in
  main|master)
    echo "Pre-commit reject: commit on default branch ($branch) not allowed."
    exit 1
    ;;
  dr/per-applet-pilot)
    # Integration branch: all surface allowed. No restrictions.
    exit 0
    ;;
  per-applet-applet/*)
    slug=${branch#per-applet-applet/}
    # Allowed surface for an applet implementer: exactly three files.
    allowed_pattern="^(plugins/applets/${slug}\.cpp|shim/include/applet_manifests/${slug}\.h|harness/tests/test_applet_${slug}\.cpp)$"

    staged=$(git diff --cached --name-only)
    for f in $staged; do
      # Hard-reject the setup-owned triad regardless of staged content.
      case "$f" in
        shim/include/HemiPluginInterface.h \
        | shim/include/applet_manifest.h \
        | shim/include/host_helpers.h \
        | shim/src/host_helpers.cpp \
        | plugins/applets/_per_applet_runtime.h \
        | harness/tests/test_hemispheres.cpp \
        | Makefile)
          echo "Pre-commit reject: implementer ($branch) cannot stage setup-owned file: $f"
          exit 1
          ;;
      esac
      if ! echo "$f" | grep -Eq "$allowed_pattern"; then
        echo "Pre-commit reject: implementer ($branch) cannot stage file: $f"
        echo "Allowed surface: plugins/applets/${slug}.cpp, shim/include/applet_manifests/${slug}.h, harness/tests/test_applet_${slug}.cpp"
        exit 1
      fi
    done

    # Ancestry guard: implementer branches must derive from dr/per-applet-pilot.
    if ! git merge-base --is-ancestor dr/per-applet-pilot HEAD 2>/dev/null; then
      echo "Pre-commit reject: $branch is not descended from dr/per-applet-pilot."
      exit 1
    fi
    exit 0
    ;;
  per-applet-host/*)
    slug=${branch#per-applet-host/}
    allowed_pattern="^(plugins/hosts/${slug}\.cpp|harness/tests/test_host_${slug}\.cpp)$"

    staged=$(git diff --cached --name-only)
    for f in $staged; do
      case "$f" in
        shim/include/HemiPluginInterface.h \
        | shim/include/applet_manifest.h \
        | shim/include/host_helpers.h \
        | shim/src/host_helpers.cpp \
        | plugins/applets/_per_applet_runtime.h \
        | harness/tests/test_hemispheres.cpp \
        | Makefile)
          echo "Pre-commit reject: host implementer ($branch) cannot stage setup-owned file: $f"
          exit 1
          ;;
      esac
      if ! echo "$f" | grep -Eq "$allowed_pattern"; then
        echo "Pre-commit reject: host implementer ($branch) cannot stage file: $f"
        echo "Allowed surface: plugins/hosts/${slug}.cpp, harness/tests/test_host_${slug}.cpp"
        exit 1
      fi
    done

    if ! git merge-base --is-ancestor dr/per-applet-pilot HEAD 2>/dev/null; then
      echo "Pre-commit reject: $branch is not descended from dr/per-applet-pilot."
      exit 1
    fi
    exit 0
    ;;
  # Preserve existing Phase 5/Phase 6 patterns (no-op on this hook;
  # the older hook content remains for those branches).
  dr/phase6-applets-plan|phase6-port/*|dr/phase5-deps-plan|phase5-dep/*)
    exit 0
    ;;
  *)
    exit 0
    ;;
esac
```

Layer 0 also seeds the empty test skeletons so implementers do NOT have to create the file (which would conflict with the allowed-surface check that only fires on staged-and-modifying).

## Layer 0: Setup commits (sequential, parent agent on `dr/per-applet-pilot`)

| Step | File(s) | Commit message |
|---|---|---|
| 0.1 | `shim/include/HemiPluginInterface.h` | `feat(shim): add versioned HemiPluginInterface ABI struct` |
| 0.2 | `shim/include/applet_manifest.h` | `feat(shim): add BusKind/BusParam manifest types` |
| 0.3 | `shim/include/host_helpers.h`, `shim/src/host_helpers.cpp` | `feat(shim): add host slot resolution + incompatible stub renderer` |
| 0.4 | `plugins/applets/_per_applet_runtime.h` | `feat(plugins): add per-applet runtime helpers (parameter assembly, bus I/O, inner-tick loop, customUi router)` |
| 0.5 | `plugins/applets/.gitkeep`, `plugins/hosts/.gitkeep`, `plugins/probes/.gitkeep` | `chore(plugins): seed directory structure` |
| 0.6 | `Makefile` | `build: add per-applet + host build macros and PILOT_APPLET_LIST` |
| 0.7 | `.git/hooks/pre-commit` (not committed; lives outside repo) | n/a — write directly to the hook file |
| 0.8 | 6 × `harness/tests/test_applet_<APPLET>.cpp` skeleton | `test(applets): seed per-applet test skeletons for pilot fan-out` |
| 0.9 | `CLAUDE.md` | `docs: deprecate top-level applets/ directory in favor of plugins/` |

Each step is a separate commit. Sequential because later steps depend on earlier headers existing.

Layer 0 success criteria:

- `make host` succeeds (host harness still builds after setup commits).
- `make test-applets` still passes (existing tests unaffected).
- `make arm` still builds the bundled `Hemispheres.o` and `Hemispheres2.o`.
- Pre-commit hook in `.git/hooks/pre-commit` accepts a no-op commit on `dr/per-applet-pilot` and rejects a setup-owned file commit on a test `per-applet-applet/_dummy` branch (verified manually before Layer 1a dispatch).

## Layer 1a: Pilot implementers (parallel, 6 implementer subagents in ONE message)

Each implementer subagent is dispatched in worktree `isolation: "worktree"` mode equivalent (`git worktree add .worktrees/per-applet-applet-<slug> -b per-applet-applet/<slug> dr/per-applet-pilot`). Model: `sonnet`.

Pilots (one Agent call per row, all in a single parent message):

| Slug | Applet | Worktree | Branch |
|---|---|---|---|
| compare | Compare | `.worktrees/per-applet-applet-compare` | `per-applet-applet/compare` |
| clockdivider | ClockDivider | `.worktrees/per-applet-applet-clockdivider` | `per-applet-applet/clockdivider` |
| vectorlfo | VectorLFO | `.worktrees/per-applet-applet-vectorlfo` | `per-applet-applet/vectorlfo` |
| cumulus | Cumulus | `.worktrees/per-applet-applet-cumulus` | `per-applet-applet/cumulus` |
| relabi | Relabi | `.worktrees/per-applet-applet-relabi` | `per-applet-applet/relabi` |
| probabilitydivider | ProbabilityDivider | `.worktrees/per-applet-applet-probabilitydivider` | `per-applet-applet/probabilitydivider` |

Each implementer prompt MUST include:

- Worktree path + branch (exact strings above).
- First-action verification commands: `pwd`, `git rev-parse --abbrev-ref HEAD`, `test -f docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md`. Abort if mismatched.
- Submodule init reminder (already done by parent dispatch, but implementer re-checks vendor reachability).
- Allowed surface: exactly three files (`plugins/applets/<APPLET>.cpp`, `shim/include/applet_manifests/<APPLET>.h`, `harness/tests/test_applet_<APPLET>.cpp`).
- Forbidden surface (enforced by pre-commit hook): everything else.
- Spec reference for the manifest schema, the `_per_applet_runtime.h` API surface, the per-applet template structure, and the per-applet hook contract table.
- Per-applet test concerns (10x ticks-per-step gotcha for Cumulus, singleton semantics for ProbabilityDivider, RelabiManager for Relabi, vec-osc dep accounting for VectorLFO).
- Verification commands: `make build/arm/<APPLET>.o` should succeed in the implementer worktree; `./build/host/test_applet_<APPLET>` should pass.
- Commit message convention: conventional commits, `feat(plugins): port <APPLET> as standalone per-applet plug-in`. Single squashable commit per implementer.

Implementer subagent contract (every implementer states this verbatim in its first action):

```
pwd     # expect: .worktrees/per-applet-applet-<slug>
git rev-parse --abbrev-ref HEAD  # expect: per-applet-applet/<slug>
test -f docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md  # expect: present
git submodule status | head -4  # expect: vendor pinned at known SHAs
```

Layer 1a success criteria:

- All 6 implementer commits land on their respective `per-applet-applet/<slug>` branches.
- Each commit's pre-commit hook check passes (allowed surface only).
- Each `build/arm/<APPLET>.o` builds cleanly inside its worktree (verified inside the worktree before the implementer reports done).
- Each `test_applet_<APPLET>` passes in the worktree.

## Layer 1b: Host implementers (parallel, 2 implementer subagents in ONE message AFTER Layer 1a integration)

Hosts run AFTER Layer 1a is integrated onto `dr/per-applet-pilot` (Layer 2). This gives the host tests real per-applet `.o` artifacts to validate against.

| Slug | Host | Worktree | Branch |
|---|---|---|---|
| hemispheres | Hemispheres host | `.worktrees/per-applet-host-hemispheres` | `per-applet-host/hemispheres` |
| quadrants | Quadrants host | `.worktrees/per-applet-host-quadrants` | `per-applet-host/quadrants` |

Each host implementer prompt MUST include:

- Worktree path + branch.
- First-action verification (same shape as Layer 1a).
- Allowed surface: exactly two files (`plugins/hosts/<HOST>.cpp`, `harness/tests/test_host_<HOST>.cpp`).
- Spec reference for the `HemiPluginInterface` ABI, the host control claim model, the `resolve_slot()` API, and the incompatible-stub render contract.
- Per-host test concerns: ABI-mismatch (`magic = 0`), wrong-guid prefix, empty-slot, focused-slot rendering (Quadrants only).
- Verification: `make build/arm/<HOST>_host.o` succeeds; `./build/host/test_host_<HOST>` passes.

Layer 1b success criteria:

- Both host commits land on their respective `per-applet-host/<slug>` branches.
- Each host `.o` builds cleanly.
- Each `test_host_<HOST>` passes in the worktree.

## Layer 2: Integration on `dr/per-applet-pilot` (sequential, parent agent)

After Layer 1a:

- Cherry-pick each of the 6 pilot commits onto `dr/per-applet-pilot` in alphabetical order by slug. Conflict expected only if two implementers somehow staged the same file; the pre-commit hook should have prevented this. If a cherry-pick fails, the implementer's commit gets investigated; do not blindly resolve.
- Run `make test-applets` clean. Baseline + 6 new per-applet test executables pass.
- Run `make arm` clean. All `.o` artifacts build.
- Run `arm-none-eabi-size` on the 6 new pilot `.o` files plus the bundled `Hemispheres.o` and `Hemispheres2.o`. Publish the table.

After Layer 1b (re-integration):

- Cherry-pick the 2 host commits.
- Re-run `make test-applets` and `make arm`.
- Run `arm-none-eabi-size` including the 2 new host `.o` files.

If ABORT-A4 triggers at this step (combined 6-pilot text > 130 KB), halt and post abort report.

## Layer 3: Hardware smoke check (sequential, parent agent with NT connected)

- Deploy each pilot standalone via `make deploy-sysex SYSEX_PLUGIN=build/arm/<APPLET>.o SYSEX_ID=0..5`. Confirm `Misc > Plug-ins > View Info` shows each as loaded.
- Capture screen via `mcp__nt_helper__show_screen` for the View Info display.
- Build a preset with Hemispheres host + 2 pilots in slot configuration. Exercise both encoders + buttons. Confirm routing matches the control-map.
- Build a preset with Quadrants host + 4 pilots. Exercise button1-4 (focus select) + L/R encoders.
- Build a preset with BOTH hosts + 6 total applets. Read ITC consumption.
- Publish ITC consumption table in the PR body.

Hardware smoke success criteria:

- All 6 pilots load standalone.
- Hemispheres host preset functions correctly with 2 pilots.
- Quadrants host preset functions correctly with 4 pilots + focused-slot select.
- Both-hosts preset either certifies or documents the ITC ceiling.

## Layer 4: Manifest schema freeze checkpoint + PR

- Post manifest-frozen confirmation in PR body. The schema in `shim/include/applet_manifest.h` is frozen as of the pilot release merge. Schema changes from mass-port onward require explicit replan.
- Open PR titled "Per-applet refactor pilot release (6 applets plus 2 hosts)" with:
  - Per-applet `.text` size table.
  - ITC consumption table.
  - Decisions list from spec.
  - Test plan checkboxes: `make arm` clean, `make test-applets` clean, all per-applet tests pass, all host routing tests pass, each pilot loads on hardware standalone, both hosts load and route correctly, both-hosts-loaded scenario tested (certified or documented constraint).

DO NOT delete bundled `Hemispheres.o` plus `Hemispheres2.o`. Cleanup release scope.

## Abort conditions (per kickoff)

If any of these fires at audit, integration, or hardware smoke, halt and post an abort report under `docs/superpowers/abort-reports/`:

- Vendor `_NT_factory` does not expose `hasCustomUi` or `customUi` (ABORT-A1). Already cleared in audit.
- Cross-slot `_NT_slot::plugin()` returns nullptr for per-applet plug-in instances on hardware (ABORT-A2). Cleared in audit; hardware smoke is the final check.
- `NT_setParameterFromAudio` does not propagate updates to other slots' internal state. Hardware-only; not exercised by the pilot release (no pilot sets parameters on other slots).
- The 6 pilots' combined `.text` exceeds 130 KB (ABORT-A4). Checked at Layer 2 integration.

## Parallelization summary

Per `~/.claude/rules/parallel-execution.md`:

- Layer 0: sequential (parent agent, setup commits).
- Layer 1a: PARALLEL (6 implementer subagents dispatched in a single message, single round-trip).
- Layer 2 (Layer 1a integration): sequential (parent cherry-pick).
- Layer 1b: PARALLEL (2 host implementer subagents dispatched in a single message).
- Layer 2 (Layer 1b integration): sequential (parent cherry-pick).
- Layer 3: sequential (hardware-only, single device).
- Layer 4: sequential (PR open).

End-to-end wallclock projection: ~Layer 0 (15-30 min) + max(pilot implementer) ~10-30 min + integration ~5 min + max(host implementer) ~15-30 min + integration ~5 min + hardware smoke ~30 min + PR ~5 min = roughly 90-130 minutes, comfortably under the 4-hour ceiling.
