# Per-applet mass-port plan

Date: 2026-05-20
Owner: Dan
Spec: `docs/superpowers/specs/2026-05-20-per-applet-mass-port-design.md`
Brainstorm: `docs/superpowers/brainstorms/2026-05-20-per-applet-mass-port-brainstorm.md`
Kickoff: `docs/superpowers/prompts/2026-05-20-per-applet-mass-port-kickoff.md`

## Parallelization strategy

The 49 applet ports are independent at the file-surface level. Each
implementer produces three files: `plugins/applets/<APPLET>.cpp`,
`shim/include/applet_manifests/<APPLET>.h`, and
`harness/tests/test_applet_<APPLET>.cpp`. The files do not overlap
between applets.

End-to-end wallclock target equals the slowest single applet port
plus per-batch integration time, NOT the sum across all 49.

Dispatch shape (per `~/.claude/rules/parallel-execution.md`):

1. One orchestrator dispatches all implementer subagents for a batch
   in a single message.
2. Each implementer runs in `isolation: "worktree"`. Each worktree
   branches from `dr/per-applet-mass-port`, NOT from `main`.
3. Each implementer commits on its `per-applet-applet/<applet>` branch
   and reports back worktree, branch, commit SHA, arm-none-eabi-size
   output, and test count.
4. Orchestrator integrates by cherry-picking parallel commits in
   completion order onto `dr/per-applet-mass-port`.
5. Each batch finishes and integrates clean before the next batch
   dispatches.

## Layer 0 (parent agent, sequential, complete)

Setup commits already landed on `dr/per-applet-mass-port`. Verified
by `make test-applets` (282 cases, 2596 assertions, all pass) and
`make arm` clean (6 pilot per-applet .o files plus 2 host plug-ins).

| Step | Commit | Notes |
| --- | --- | --- |
| Layer 0.1 | `06b7b7a` | PerInstanceState in _AppletInstance |
| Layer 0.2 | `06b7b7a` | render_view_with_offset helper |
| Layer 0.3 | `06b7b7a` | per-output mode parameter names |
| Layer 0.5 | `06b7b7a` | gfx_offset_y shim global + helpers |
| Layer 0.6 | (this PR) | spec, brainstorm, plan docs |
| Integration | `86ce3eb` + (this PR) | Makefile ALL_APPLET_LIST, pre-commit hook update |

## Batched implementer dispatch

| Batch | Count | Wallclock target | Soft ceiling | Abort threshold |
| --- | --- | --- | --- | --- |
| 1a (canary) | 5 | 60-90 min | 2 hr | 25% first-run failure |
| 1b | 7 | 60-90 min | 2 hr | 25% |
| 1c | 8 | 60-90 min | 2 hr | 25% |
| 2 | 3 | 60-90 min | 2 hr | 25% |
| 3 | 13 | 60-90 min | 2 hr | 25% |
| 4 | 8 | 60-90 min | 2 hr | 25% |
| 5 | 5 | 60-90 min | 2 hr | 25% |

Canary rationale: Batch 1a at N=5 stays within the pilot-validated
range (pilot dispatched 6 implementers successfully). If 1a integrates
clean, 1b at N=7 confirms the pattern holds slightly above pilot. 1c
at N=8 sets the post-canary ceiling.

## Per-batch worklist

Each applet below lists its batch, vendor deps, and any per-applet
notes that supplement the canonical recipe in the spec. The
implementer prompt is a paragraph-per-applet pulled from this table
plus the canonical recipe.

### Batch 1a (5)

- AttenuateOffset, Binary, Button, Logic, Switch. All trivial.
  Button/Switch/GatedVCA have `OnDataRequest() == 0`; the test asserts
  this directly instead of round-tripping through a pack helper.

### Batch 1b (7)

- Brancher, Burst, Calculate, EnvFollow, GameOfLife, GateDelay,
  GatedVCA. All trivial; GatedVCA `OnDataRequest()` returns 0.

### Batch 1c (8)

- RndWalk, Schmitt, ShiftGate, Slew, Stairs, TLNeuron, Trending,
  Voltage. Stairs uses the model-the-multiplier test shape; Voltage's
  pack helper zeroes bit 9.

### Batch 2 (3)

- VectorEG, VectorMod, VectorMorph. Mirror pilot VectorLFO pattern.
  Header-only vec_osc dependency; no ARM .cpp link.

### Batch 3 (13)

- DualQuant, OffsetQuant, MultiScale, ScaleDuet, EnsOscKey, Calibr8,
  Carpeggio, Chordinator, EnigmaJr, Pigeons, Squanch, Shredder,
  Strum. All use `HS::Quantize()`. EnigmaJr pulls
  `enigma/TuringMachine.h` + `HSMIDI.h` shim stubs; its
  `user_turing_machines[40]` global is shared per-.o (intentional).

### Batch 4 (8)

Per-applet 10x-ticks categorization carried into each implementer
prompt:

- Metronome: bus-level fire-count safe.
- ResetClock: state-injection only. Use `step_n_inner_ticks` harness
  helper. Forbid bus-level fire-count assertions.
- Shuffle: state-injection only.
- Xfader: bus-level fire-count safe.
- Scope: model-the-multiplier (assert final-tick value).
- ClkToGate: model-the-multiplier (assert ClockOut presence not count).
- ClockSkip: model-the-multiplier (seed RNG or force `p_mod = 100`).
- PolyDiv: state-injection only.

### Batch 5 (5)

- ADEG: trivial.
- ADSREG: trivial. `Proportion` remains a free function for nested
  `MiniADSR` lookup.
- RunglBook: model-the-multiplier (per pilot RunglBook RB3).
- LowerRenz: links `streams_resources.o` and
  `streams_lorenz_generator.o`. `LorenzGeneratorManager` singleton
  shared per-.o (intentional).
- Combin8: uses `CVInputMap` via base; `gfxDisplayInputMapEditor` in
  shim.

## Per-implementer prompt template

Each implementer subagent receives the prompt verbatim from the
kickoff doc plus per-applet notes from the table above. Worktree
created by parent agent ahead of dispatch:

```sh
git worktree add .worktrees/per-applet-applet-<APPLET> \
    -b per-applet-applet/<APPLET> dr/per-applet-mass-port
git -C .worktrees/per-applet-applet-<APPLET> submodule update --init --recursive --depth=1
```

Submodule init MUST run inside the new worktree. Worktrees do not
inherit submodule state.

## Integration loop

After each batch:

1. Cherry-pick implementer commits onto `dr/per-applet-mass-port` in
   completion order.
2. `make test-applets` clean.
3. `make build/arm/<APPLET>.o` clean for each new applet.
4. Append `arm-none-eabi-size` row to PR body draft.
5. Conflict investigation: per-applet files are non-overlapping;
   conflicts signal an implementer error to investigate.

## Hardware smoke (Layer 3)

After all 49 integrated:

1. Deploy every per-applet .o via sysex.
2. Misc > Plug-ins > View Info: all 49 + 6 pilots + 2 hosts Passed.
3. Hemispheres host with 2 random per-applet plug-ins from different
   batches. Verify cross-batch interoperability.
4. Quadrants host with 4 per-applet plug-ins. Confirm 2x2 grid renders
   correctly (Layer 0.5 Y-offset shim is the prereq).
5. Both-hosts-loaded stress preset. Read total ITC; certify or
   document constraint.

## PR (Layer 4)

Title: "Per-applet refactor mass-port release (49 applets)". Body:

- Per-batch `.text` size tables (collated from implementer reports).
- ITC consumption table from hardware smoke.
- Pilot-vs-mass-port combined ITC budget comparison.
- Test plan checkboxes.

## Wallclock structure

Per the kickoff, end-to-end Layer 0 + 7 batches + smoke + PR runs
10-12 hours unattended. Three human checkpoints structure the
release:

| Checkpoint | When | Type |
| --- | --- | --- |
| Post-Layer-0 | Setup commits landed; `make test-applets` + `make arm` green | Hard halt |
| Per-batch | Batch cherry-picked; tests + arm size table posted | Soft halt; auto-proceed if clean |
| Post-hardware-smoke | All 49 deployed; cross-batch preset verified | Hard halt |

## Abort conditions

Halt and post abort report under `docs/superpowers/abort-reports/`:

- Any Layer 0 prerequisite fails to land cleanly in 2 attempts.
- More than 5 of 49 implementers fail their first run. Indicates spec
  or shim defect rather than implementer error.
- Both-hosts-loaded preset overflows ITC at smoke.
- Layer 0.5 Y-offset shim regresses any existing applet's render.
