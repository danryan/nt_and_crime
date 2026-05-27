# Kickoff: semantic IWYU sweep

Date: 2026-05-26
Tracks: GitHub issue #21
Origin: deferred item D7 from `docs/superpowers/brainstorms/2026-05-23-cleanup-brainstorm.md`

## Goal

Remove genuinely unused `#include` directives across `shim/`, `plugins/`, and `harness/` using semantic analysis (include-what-you-use reasoning), not filename heuristics. Add a missing direct include only where a TU relies on a symbol it gets transitively. Behavior must not change: every build artifact's `.text` is byte-identical before and after.

## Why this exists

The 2026-05-23 cleanup release ran a shallow heuristic for its Phase E (does the include's basename appear as a symbol token in the TU?) and found zero candidates, so it shipped nothing. That heuristic is weak: it mis-judges any include whose used symbol does not match the header name. The load-bearing counterexample is `shim/include/HemisphereApplet.h`, which includes `OC_strings.h` and uses `OC::Strings::capital_letters`; the token `OC_strings` never appears in the file, so the heuristic would falsely flag the include as unused. A correct pass resolves namespace and macro names.

This is polish, not a defect. The tree is already clean at the obvious level. Keep effort proportional; do not invent work.

## Scope

In scope:

- Every `.cpp` and `.h` under `shim/include/`, `shim/src/`, `plugins/`, `harness/`.
- Removing an `#include` that nothing in the TU uses.
- Adding a direct `#include` for a symbol the TU uses only transitively (IWYU's other half), but only when it improves robustness; do not churn.

Out of scope:

- `vendor/` (read-only submodules; never edit).
- `build/`.
- Any change that alters behavior or any artifact's `.text`.
- Reordering includes for style. This is about presence, not order.
- The `shim/include/` vs `shim/src/` directory split (separate concern; see issue #20 lineage).

## Method

- Use a real tool that resolves symbols, e.g. `include-what-you-use` with project-tuned mappings, or hand-audit per include with namespace/macro awareness. Do NOT apply a tool blindly: IWYU tooling over-suggests on shim/vendor boundaries. Treat every suggestion as a hypothesis to verify by building.
- The lorenz forwarding-bridge headers were removed in the vendor de-dup; vendor `.cpp` files are now compiled in place from `$(HEM_SRC_DIR)`. If any forwarding bridge survives elsewhere in `shim/src/`, it exists on purpose (vendor `.cpp` files use bare-name includes); do not "fix" those.
- The shim's job is to satisfy unmodified vendor applet headers. An include in a shim header may exist to put a name in scope for a downstream vendor header, not for the shim header's own body. Confirm no vendor applet header breaks before removing such an include: `make test-applets` plus `make arm` is the check.

## Workflow

Standard brainstorm then spec then plan under `docs/superpowers/`, per CLAUDE.md "Document layout" and "Workflow".

- Brainstorm: per-directory candidate inventory. For each proposed removal, cite `file:line`, name the symbol that proves the include is unused (or the transitive path that proves it is needed), and the build target that covers the TU.
- Spec: the canonical recipe (audit one include, build, confirm `.text` unchanged, commit or revert) plus per-directory entries.
- Plan: parallel by directory. `shim/include/` sweeps FIRST and sequentially, because its headers are included by every other directory; once it lands, `shim/src/`, `plugins/`, and `harness/` sweep in parallel via subagent dispatch in isolated worktrees branched from the feature branch.

## Verification gates

- Per commit (one logical sweep per commit, one commit per directory): the matching `make` target for that directory builds clean.
- Aggregate: `make test-applets`, `make arm`, `make test` all pass.
- Aggregate: `arm-none-eabi-readelf -W -S` on `build/arm/Hemispheres_host.o`, `build/arm/Quadrants_host.o`, and every per-applet `.o` shows `.text` byte-identical to the pre-sweep baseline. An include removal that shifts `.text` means the include was load-bearing (forced an inline instantiation, a template, a constant); revert it.

## Stop conditions

- Any `.text` size delta that a revert does not resolve: halt and escalate.
- A removal that breaks a vendor applet header's compile: revert; the include is part of the shim contract.
- The sweep turns up a real bug (not just an unused include): halt and escalate rather than fixing inline.
- Net candidate count is tiny after honest analysis: that is the expected outcome. Document the negative result and close; do not manufacture removals to justify the pass.

## Commit convention

Conventional Commits. One commit per directory sweep:

- `refactor(shim): drop unused includes in shim/include`
- `refactor(shim): drop unused includes in shim/src`
- `refactor(plugins): drop unused includes in plugins/`
- `refactor(harness): drop unused includes in harness/`

Footer: `Refs: #21`.
