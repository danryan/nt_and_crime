# Epic #72 Batch 5: BitBeat (design)

Vendor pin: `7800d929`. Audit: #71. Epic: #72.

## Scope

Port the Hemisphere applet BitBeat (`applets/BitBeat.h`) to a per-applet NT
plug-in. A dual bytebeat generator (`peaks::ByteBeat bytebeat[2]`). Standard
per-applet recipe plus the already-shipped `peaks_bytebeat` vendor link.

## Vendor dep

BitBeat uses `peaks::ByteBeat` (`ProcessSingleSample` / `Configure` /
`ProcessAlgorithm`), whose DSP lives in `peaks_bytebeat.cpp`. That source is
already linkable: the OC apps BYTEBEATGEN, ASR, and QQ link it. BitBeat adds the
same link:

```make
VENDOR_DEPS_BitBeat          := build/arm/vendor_src/peaks_bytebeat.o
```

plus `peaks_bytebeat.cpp` in the host-side vendor sources (the streams/LowerRenz
precedent). `util/util_history.h` is header-only and portable (DQ uses it);
`OC::Strings::bytebeat_equation_names` is already in the shim
(`OC_strings.h` + `globals.cpp`, added for BYTEBEATGEN).

## Applet

Reference recipe: `ClockDivider`. `using Manifest = per_applet::BitBeat;`,
member `BitBeat applet;`, GUID `HmBb`, `kNumParams = 6`. Inputs Reset1/Reset2
(gate); outputs Beat1/Beat2 (`BusKind::cv`: the bytebeat output is a bipolar
full-scale analog/modulation signal `(sample - 32768) * HEMISPHERE_3V_CV /
32768`, not a gate -- the BYTEBEATGEN precedent).

## Coverage

The bytebeat state advances inside `if (Clock(ch))`, so SHAPE 2 (drive
Reset/Clock, assert Beat output appears, round-trip the pack). Equation 0 needs
`t > 1024` to produce non-zero output, so step enough buffers (150 steps x 10
inner ticks = 1500 t).

## Spec footer

### Recipe spot-check

Shipped per-applet recipe plus a copy of the existing BYTEBEATGEN vendor link.
No new shim subsystem.

### Per-entry verification (traced against vendor `7800d929`)

- BitBeat (`applets/BitBeat.h:24`): `class BitBeat : public HemisphereApplet`,
  `#include "../peaks_bytebeat.h"` line 21, `peaks::ByteBeat bytebeat[2]` line
  239, `ProcessSingleSample` line 351, SetHelp Reset1/Reset2/Beat1/Beat2,
  OnDataReceive line 193. CONSISTENT with the manifest and the peaks link.

### Shim prereq verification

`peaks_bytebeat` (linked, shipped), `util/util_history.h` (header-only),
`OC::Strings::bytebeat_equation_names` (shim). No new shim subsystem; only the
Makefile vendor-dep link plus any additive icon stubs.
