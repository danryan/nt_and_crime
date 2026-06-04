# Epic #72 Batch 5 plan: BitBeat

Spec: `docs/superpowers/specs/2026-06-04-epic72-bitbeat-design.md`.
Audit: #71. Epic: #72. Vendor pin `7800d929`.

## Single-applet port

BitBeat is one unit, no fan-out. One implementer worked directly on
`dr/epic72-bitbeat` (branched from `main`).

- Layer 0 (none new): `peaks_bytebeat` is already linkable; BitBeat just adds the
  `VENDOR_DEPS_BitBeat` link (the BYTEBEATGEN precedent) plus the host-side
  vendor source.
- Layer 1: the BitBeat manifest + `.cpp` + test.
- Layer 2 (verification): full `make test-applets` green (the global host-side
  `peaks_bytebeat.cpp` addition affects all host builds, so the full suite is
  the gate, not a spot check); `BitBeat.o` ARM clean with `peaks::ByteBeat`
  symbols RESOLVED. Hardware ADD smoke check post-PR.

## Abort budget

- A `peaks::ByteBeat` symbol unresolved in the ARM `.o` means the
  `VENDOR_DEPS_BitBeat` link is wrong -> fix. Did not fire.
- Full-suite regression from the global host-srcs change -> halt. Did not fire.

## PR scope

Per the epic, batch 5 ships BitBeat and EnvSeq as separate PRs. This branch is
BitBeat only.
