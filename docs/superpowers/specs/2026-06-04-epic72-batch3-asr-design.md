# Epic #72 Batch 3: ASR (design)

Vendor pin: `7800d929`. Audit: #71. Epic: #72.

## Scope

Port the Hemisphere applet ASR (`applets/ASR.h`) to a per-applet NT plug-in.
One applet plus one small shim subsystem (a static-arena `operator new`).

## Build-token rename

A different vendor app, the O_C full-screen ASR (`plugins/apps/ASR.cpp` ->
`build/arm/ASR.o`, `OC_APP_LIST`, `VENDOR_DEPS_ASR`), already owns the token
`ASR`. The Hemisphere applet therefore ships under build token `ASRHemi`
(`plugins/applets/ASRHemi.cpp`, `applet_manifests/ASRHemi.h` ->
`struct per_applet::ASRHemi`, `test_applet_ASRHemi.cpp`, `ALL_APPLET_LIST`
entry `ASRHemi`, `build/arm/ASRHemi.o`). The on-device display `name` is still
"ASR". GUID `HmAs`.

## Layer 0: static-arena operator new (shipped on the batch-1 branch)

ASR's `RingBufferManager::get()` does `new RingBufferManager` for a one-time
singleton. The NT has no heap and the default ARM `operator new` stub returns
nullptr, which hard-faults the device when the singleton is dereferenced at
add-algorithm time. `shim/src/cxx_runtime_stubs.cpp` now provides an opt-in bump
allocator into a fixed 4 KB static arena, enabled per-TU by defining
`NT_HEM_NEED_HEAP_ARENA` before the shim aggregation. A TU that does not opt in
keeps the unchanged nullptr stub. The same fix de-risks the already-shipped
CVRecV2 (batch 1). This batch branches from `dr/epic72-batch1` so it inherits
the arena; the batch-3 PR should merge after #73.

`HSRingBufferManager.h` itself needs no port: it is header-only and compiles
against the shim as-is (`HEM_SIDE`, `LEFT_HEMISPHERE`, `RIGHT_HEMISPHERE`,
`HEMISPHERE_MAX_INPUT_CV`, `constrain` are all shim-available). It is included
unmodified from `applets/ASR.h`.

## Applet recipe delta

`plugins/applets/ASRHemi.cpp` begins with `#define NT_HEM_NEED_HEAP_ARENA 1`
before any include. Otherwise the standard per-applet recipe (reference:
`ClockDivider`). Manifest exposes 4 inputs (Clock gate, Freeze gate, CV, Index)
and 2 outputs (quantized Out A/B); `kNumParams = 4 + 2*2 = 8`. The 4-input
layout is required because the vendor Controller reads both `Clock(0)` (a gate,
from `HS::frame.clocked`) and `In(0)` (a CV) on the same physical Hemisphere
jack, and NT buses carry a single signal type, so the gate and CV paths get
separate manifest inputs (the DualQuant precedent). Braids quantizer is
shim-baseline (no `VENDOR_DEPS`).

## 10x / ADC-lag coverage

ASR samples on `EndOfADCLag()` after `StartADCLag()` on a clock edge. The test
drives a clock pulse, clears the bus, then steps enough buffers to drain
`HEMISPHERE_ADC_LAG` (CLAUDE.md "Single-shot gate tests must clear the bus
between steps"), then asserts output. SHAPE 2 coverage (round-trip plus state
injection), no bus-level fire counts.

## Spec footer

### Recipe spot-check

Standard per-applet recipe plus the `NT_HEM_NEED_HEAP_ARENA` opt-in. 70 applets
ship through the recipe. The arena Layer 0 is proven: CVRecV2.o and ASRHemi.o
resolve `operator new` to the arena (defined, real address), DivSeq.o (non-arena)
keeps the nullptr stub, host tests green.

### Per-entry verification (traced against vendor `7800d929`)

- ASR (`applets/ASR.h:28`): `class ASR : public HemisphereApplet`,
  `#include "../HSRingBufferManager.h"` line 26, `buffer_m = RingBufferManager::get()`
  (heap singleton), `OnDataRequest` packs index@[0,8) + GetScale(0)@[8,8) +
  GetScale(1)@[16,8), SetHelp Clock/Freeze/CV/Index/Out/Out. CONSISTENT with the
  manifest and pack helper.

### Shim prereq verification

The only genuinely new shim work is the arena `operator new` (Layer 0). braids
quantizer, `Quantize`/`GetScale`/`SetScale`/`HS::QuantizerEdit`, the ADC-lag
helpers, and `HSRingBufferManager.h` are all already available. No `VENDOR_DEPS`.
