# Kickoff: vendor de-duplication for lorenz and quant

Date: 2026-05-26
Tracks: GitHub issue #20
Origin: deferred item D6 from `docs/superpowers/brainstorms/2026-05-23-cleanup-brainstorm.md`

## Goal

Remove the verbatim copies of vendor lorenz and quant source that live under `shim/`, building the vendor source directly where it is byte-identical to the copy. Behavior must not change: every affected build artifact's `.text` is byte-identical before and after. The win is roughly 2474 fewer duplicated lines plus two forwarding-bridge headers, and the removal of the `shim/include/` vs `shim/src/` split friction those copies forced.

## Why this exists

`shim/` carries hand-maintained copies of vendor files that the shim could include directly from `vendor/O_C-Phazerville/software/src`. Each copy is a place where a vendor update silently diverges from what the shim builds. The `shim/include/` plus `shim/src/` split with forwarding-bridge headers exists largely to host these copies; collapsing the duplication collapses that friction with it. This is polish, not a defect. The tree builds and ships today. Keep effort proportional and do not manufacture work.

## Scope

Three units, ranked by risk. They are independent at the file level except where noted.

Unit 1, lorenz (clean win, lowest risk). All four files are byte-identical to vendor (confirmed by `diff -q` in the issue):

- `shim/include/lorenz/streams_lorenz_generator.h`
- `shim/include/lorenz/streams_resources.h`
- `shim/src/lorenz/streams_lorenz_generator.cpp`
- `shim/src/lorenz/streams_resources.cpp`
- plus the two forwarding-bridge headers `shim/src/lorenz/streams_lorenz_generator.h` and `shim/src/lorenz/streams_resources.h`.

Vendor source sits at `vendor/O_C-Phazerville/software/src/streams_{lorenz_generator,resources}.{h,cpp}`. LowerRenz is the only consumer; it links the copied objects via `VENDOR_DEPS_LowerRenz` in the Makefile (line 291). Plan: delete all four copies and both bridges, build the vendor `.cpp` files directly into LowerRenz's `.o`, and add `-Ivendor/O_C-Phazerville/software/src` to that build so the vendor cpps' bare-name includes resolve against vendor headers.

Unit 2, quant headers (clean win, low risk). Three of three are byte-identical to vendor:

- `shim/include/quant/braids_quantizer.h`
- `shim/include/quant/braids_quantizer_scales.h`
- `shim/include/quant/OC_scales.h`

`shim/include/quant/MIDIQuantizer.h` is shim-invented, not a duplicate; keep it. Plan: drop the three duplicated headers and point quant consumers at the vendor headers via include path. Quant consumers include EnigmaJr, and the MultiScale and Calibr8 manifests; the brainstorm enumerates the full set.

Unit 3, quant cpps (real divergence, gated and possibly deferred). `shim/src/quant/braids_quantizer.cpp` and `shim/src/quant/OC_scales.cpp` are NOT byte-identical to vendor. They drop `OC_options.h` and `util/util_misc.h` includes, add `util/util_macros.h`, add explicit field initialization, remove `#ifdef NORTHERNLIGHT` branches, and change one local from `constexpr` to `static constexpr`. `shim/src/quant/q_engine.cpp` is shim-invented; keep it. Path 1 from the issue (shim stubs for `OC_options.h` and `util/util_misc.h`, then build vendor cpps verbatim) is in scope only if a divergence audit clears it. The explicit field-init change must be classified as vendor-bug-fix versus shim-convention before any verbatim adoption; if it is a real fix, do not regress it. If the audit does not clear cleanly, take Path 2 (keep the shim cpps, ship only Units 1 and 2) and defer Unit 3 to its own issue.

Out of scope:

- `vendor/` (read-only submodules; never edit).
- `build/`.
- Any change that alters behavior or any artifact's `.text`.
- Upstreaming the cleanup to vendor (issue Path 3; loses control, depends on merge cadence).
- A broader `shim/include/` vs `shim/src/` reorganization beyond removing the bridges these copies forced.

## Workflow

Standard brainstorm then spec then plan under `docs/superpowers/`, per CLAUDE.md "Document layout" and "Workflow".

- Brainstorm: re-confirm each `diff -q` result against the pinned vendor SHA at audit time, enumerate every quant consumer with `file:line`, and classify the Unit 3 field-init divergence with a verdict (fix or convention) plus evidence.
- Spec: the canonical recipe (capture the baseline `.text` hash, drop the copy, wire the vendor source, rebuild, confirm the hash is unchanged, commit or revert) plus per-unit entries and the spec footer (recipe spot-check, per-entry verification, shim prereq verification).
- Plan: Units 1 and 2 are independent and parallelizable via subagent dispatch in isolated worktrees branched from the feature branch. Unit 3 is sequenced after Unit 2 and gated on the divergence audit; mark it as deferrable.

## Verification gates

The correctness contract is byte-identical `.text`, not merely a clean build. Compare section contents, not just sizes.

- Baseline before any change: for each affected `.o` (`build/arm/LowerRenz.o`, every quant-using applet `.o`), extract and hash the section, for example `arm-none-eabi-objcopy -O binary --only-section=.text build/arm/LowerRenz.o /tmp/lowerrenz.text && shasum -a 256 /tmp/lowerrenz.text`.
- Per commit (one unit per commit): the affected `.o` rebuilds and its `.text` hash matches the baseline. A hash change means the copy and the vendor source were not actually identical at build time; investigate before proceeding.
- Aggregate: `make test-applets`, `make arm`, and `make test` all pass.

## Stop conditions

- A `.text` hash delta on any affected `.o` that the de-dup was supposed to leave untouched: halt and investigate the divergence rather than accepting the new artifact.
- A `diff -q` at audit time that no longer reports byte-identical (the vendor SHA moved since the issue was filed): re-scope that file out of the clean-win path and treat it like Unit 3.
- Unit 3 divergence audit finds the field-init change is a real vendor-bug-fix: do not adopt verbatim vendor cpps; keep the shim cpps and defer Unit 3.
- The work turns up a genuine bug rather than a duplication: halt and escalate rather than fixing inline.

## Commit convention

Conventional Commits. One commit per unit:

- `refactor(shim): build vendor lorenz source directly, drop copies`
- `refactor(shim): drop duplicated quant headers, include vendor directly`
- `refactor(shim): build vendor quant cpps directly` (only if Unit 3 audit clears)

Footer: `Refs: #20`.
