# Plan G: Tier 2 Applet Expansion

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the `Hemispheres` applet enum with three named Tier 2 applets (Brancher, TLNeuron, GateDelay) plus a stretch batch of simple utilities (Binary, Button, ClkToGate, Compare, GatedVCA, Cumulus). Each applet must compile against the shim, fit in `kMaxAppletSize` budget, render cleanly with the header banner, and pass per-applet hardware behavior verification.

**Architecture:** No shim refactor expected for the named three. Each applet is added by appending to `HemispheresFactory.h` (include + enum entry + factory table + enum string) in alphabetic position. Build, deploy via `make deploy-sysex`, hardware-verify. Any unstubbed Phazerville globals encountered during compile become per-task shim additions (stubs in `shim/include/` or `shim/src/globals.cpp`).

**Tech Stack:** Same as Plan F (C++11 arm-none-eabi, vendor headers, hemispheres_shim.h, push_plugin_to_device.py).

**Pre-req:** Plan F merged to main (commit `e44d276`).

---

## Scope decisions

| Question | Decision | Reason |
|----------|----------|--------|
| Named three first, stretch second | Yes | Validates incremental workflow before bulk |
| Per-applet commit | One commit per applet | Easy bisect if a specific applet breaks the build or hardware |
| Add unstubbed shim helpers as encountered | Per-task | Plan F's worst-case sram analysis confirms growth is cheap |
| Hardware verify per applet before next | Yes | Catches Phazerville assumptions early |
| Stretch task can be partial | Yes | Whatever lands cleanly ships; complex ones get deferred to Tier 3 |
| Update plug-in description? | No | "pick two applets" stays generic |
| Serialise selectors by applet name string, not enum index | Yes | Decouples preset compatibility from enum order. Enum can grow/reorder without silently swapping applets in saved presets. |
| Backwards compat with Plan F preset format | No | Plan F just shipped; no production preset corpus to preserve. Plan F-saved presets fail to restore selector state and boot to default Empty/Empty. Cleaner code than dual-format support. |

---

## Per-applet workflow (applies to every task)

The same five steps run for every applet:

- [ ] **Step 1: Append header include**

In `shim/include/HemispheresFactory.h`, alphabetically insert the include below the existing block:

```cpp
#include "<AppletName>.h"
```

- [ ] **Step 2: Add enum entry**

In `enum AppletIndex`, insert `kApplet<AppletName>` in alphabetic order after `kAppletEmpty`. Keep `kAppletCount` as the final entry.

- [ ] **Step 3: Add factory table entry + enum string**

In `applet_factory`'s table, add `&make_applet<AppletName>` in the same position. In `applet_enum_strings`'s `names[]`, add the vendor `applet_name()` string (max 9 chars per Phazerville convention; trim if needed).

- [ ] **Step 4: Build**

```bash
make arm
```

If the compile fails:

1. Identify the missing symbol or header.
2. Decide whether to stub it in `shim/include/` (lightweight) or skip the applet (defer to Tier 3).
3. Stub or defer, then re-run build.

After clean build, verify Hemispheres.o symbol set:

```bash
arm-none-eabi-nm -u build/arm/Hemispheres.o
```

Should match the Plan F set: only NT firmware externals (`NT_*`, json parse/stream methods, `memset`, `strlen`, `_GLOBAL_OFFSET_TABLE_`).

Run host tests to catch shim regressions:

```bash
make test
```

Expected: `gainCustomUI/zero_signal.yaml` still passes.

- [ ] **Step 5: Hardware deploy + verify**

```bash
make deploy-sysex
```

On the NT:

1. Add `Hemispheres` algo to a preset (or use existing slot).
2. Setup page -> Left applet -> pick the new applet.
3. Verify header banner shows the applet name at top-left.
4. Verify View() draws below y=12 (no clipping under the dotted underline).
5. Turn L encoder -> applet responds (cursor or value, depending on edit mode).
6. Press L encoder button -> edit mode toggles.
7. Patch I/O per applet's Help labels; verify expected behavior on outputs.
8. Save preset, reload preset -> applet state and selector persist.

- [ ] **Step 6: Commit**

```bash
git add shim/include/HemispheresFactory.h <any new shim stubs>
git -c commit.gpgsign=false commit -m "applets: add <AppletName> to Hemispheres enum"
```

If shim stubs were added, mention them in the commit body.

---

## Task 0: Compile-probe harness

**Files:**
- Create: `harness/scripts/probe_applet.sh`

Optional but speeds audit. A small script that takes an applet header name and tries to compile a stub TU that includes it through the shim. Surfaces missing dependencies before committing changes to `HemispheresFactory.h`.

- [ ] **Step 1: Write the probe script**

```bash
#!/usr/bin/env bash
# Usage: probe_applet.sh <AppletName>
# Tries to compile a TU that includes the vendor applet header through the shim
# and instantiates the class once, so the compiler resolves all references.
set -euo pipefail

NAME="${1:?usage: probe_applet.sh <AppletName>}"

TMP="$(mktemp -t probe_${NAME}.XXXXXX.cpp)"
trap "rm -f '$TMP'" EXIT

cat > "$TMP" <<EOF
#include "HemisphereApplet.h"
#include "${NAME}.h"

// Force instantiation so every reachable inline definition is checked.
namespace { ${NAME} _probe_${NAME}; }
EOF

arm-none-eabi-c++ -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
    -mthumb -fno-rtti -fno-exceptions -fno-threadsafe-statics \
    -Os -fPIC -Wall \
    -Ivendor/distingNT_API/include \
    -Ishim/include \
    -Ivendor/O_C-Phazerville/software/src/applets \
    -c -o /dev/null "$TMP"

echo "probe: ${NAME} compiles clean"
```

- [ ] **Step 2: Make executable**

```bash
chmod +x harness/scripts/probe_applet.sh
```

- [ ] **Step 3: Smoke-test against a known-good applet**

```bash
./harness/scripts/probe_applet.sh Logic
```

Expected: `probe: Logic compiles clean`.

- [ ] **Step 4: Commit**

```bash
git add harness/scripts/probe_applet.sh
git -c commit.gpgsign=false commit -m "harness: probe_applet.sh for per-applet shim compile audit"
```

---

## Task 0.5: Switch selector serialisation to applet name string

**Files:**
- Modify: `shim/include/HemispheresFactory.h`
- Modify: `shim/include/hemispheres_shim.h`

Before any applet is added, change the serialise/deserialise contract so preset compatibility no longer depends on enum index. Serialise writes the applet name string only; deserialise looks the name up in `applet_enum_strings()` and resolves to the current enum index. Plan F-saved presets (integer-only) restore both selectors to Empty.

- [ ] **Step 1: Add name-to-index lookup**

In `shim/include/HemispheresFactory.h`, add after `applet_enum_strings`:

```cpp
inline int applet_index_for_name(const char* name) {
    const char* const* names = applet_enum_strings();
    for (int i = 0; i < kAppletCount; ++i) {
        const char* a = names[i];
        const char* b = name;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;  // unknown name -> caller should leave applet at default
}
```

(Custom strcmp avoids pulling in extra libc symbols beyond `memset`/`strlen` we already use.)

- [ ] **Step 2: Confirm `_NT_jsonStream::addString` and `_NT_jsonParse::string` exist**

```bash
grep -n "addString\|addMemberString" vendor/distingNT_API/include/distingnt/serialisation.h
grep -nE "\bstring\(|parseString" vendor/distingNT_API/include/distingnt/serialisation.h
```

If either is absent, write a small wrapper that emits/parses a quoted ASCII string through the available primitives. Confirm the wrapper covers the applet name length (max ~10 chars).

- [ ] **Step 3: Rewrite `HemispheresShim::serialise`**

Replace the existing `sel_l`/`sel_r` integer writes with name writes:

```cpp
static void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
    auto* alg = static_cast<HemispheresInstance*>(self);
    uint64_t l = alg->left->OnDataRequest();
    uint64_t r = alg->right->OnDataRequest();
    stream.addMemberName("sel_l");        stream.addString(applet_enum_strings()[alg->cached_idx_left]);
    stream.addMemberName("sel_r");        stream.addString(applet_enum_strings()[alg->cached_idx_right]);
    stream.addMemberName("hem_left_hi");  stream.addNumber((int)(uint32_t)(l >> 32));
    stream.addMemberName("hem_left_lo");  stream.addNumber((int)(uint32_t)(l & 0xFFFFFFFFu));
    stream.addMemberName("hem_right_hi"); stream.addNumber((int)(uint32_t)(r >> 32));
    stream.addMemberName("hem_right_lo"); stream.addNumber((int)(uint32_t)(r & 0xFFFFFFFFu));
}
```

Note `sel_l`/`sel_r` keep their names but now carry strings, not numbers. Plan F-saved presets will fail the `parse.string(...)` call on those keys; deserialise treats the unmatched parse as "unknown" and leaves the selector at default Empty.

- [ ] **Step 4: Rewrite `HemispheresShim::deserialise`**

Replace the parse loop and post-loop swap block:

```cpp
static bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
    auto* alg = static_cast<HemispheresInstance*>(self);
    int num_members = 0;
    if (!parse.numberOfObjectMembers(num_members)) return false;
    char name_l[16] = {0};
    char name_r[16] = {0};
    int lhi = 0, llo = 0, rhi = 0, rlo = 0;
    bool got_lhi = false, got_llo = false, got_rhi = false, got_rlo = false;
    for (int i = 0; i < num_members; ++i) {
        if      (parse.matchName("sel_l"))        { if (!parse.string(name_l, sizeof(name_l))) return false; }
        else if (parse.matchName("sel_r"))        { if (!parse.string(name_r, sizeof(name_r))) return false; }
        else if (parse.matchName("hem_left_hi"))  { if (!parse.number(lhi)) return false; got_lhi = true; }
        else if (parse.matchName("hem_left_lo"))  { if (!parse.number(llo)) return false; got_llo = true; }
        else if (parse.matchName("hem_right_hi")) { if (!parse.number(rhi)) return false; got_rhi = true; }
        else if (parse.matchName("hem_right_lo")) { if (!parse.number(rlo)) return false; got_rlo = true; }
        else                                      { if (!parse.skipMember()) return false; }
    }
    int idx_l = applet_index_for_name(name_l);
    int idx_r = applet_index_for_name(name_r);
    if (idx_l >= 0 && (uint8_t)idx_l != alg->cached_idx_left) {
        hemispheres_swap(alg->left, alg->sram_left, (uint8_t)idx_l, HS::LEFT_HEMISPHERE);
        alg->cached_idx_left = (uint8_t)idx_l;
    }
    if (idx_r >= 0 && (uint8_t)idx_r != alg->cached_idx_right) {
        hemispheres_swap(alg->right, alg->sram_right, (uint8_t)idx_r, HS::RIGHT_HEMISPHERE);
        alg->cached_idx_right = (uint8_t)idx_r;
    }
    if (got_lhi && got_llo) {
        uint64_t l = ((uint64_t)(uint32_t)lhi << 32) | (uint64_t)(uint32_t)llo;
        alg->left->OnDataReceive(l);
    }
    if (got_rhi && got_rlo) {
        uint64_t r = ((uint64_t)(uint32_t)rhi << 32) | (uint64_t)(uint32_t)rlo;
        alg->right->OnDataReceive(r);
    }
    return true;
}
```

Empty name buffers (no `sel_l`/`sel_r` matched) resolve to `applet_index_for_name("") == -1`, so neither swap fires. Default-constructed applets stay (Empty/Empty for fresh slot, or whatever the slot held).

- [ ] **Step 5: Build + host test**

```bash
make arm && make test
```

- [ ] **Step 6: Hardware verify new preset round-trips**

Deploy. Pick Logic + Calculate. Save preset, reload preset, confirm both applets restore via name key.

- [ ] **Step 7: Commit**

```bash
git add shim/include/HemispheresFactory.h shim/include/hemispheres_shim.h
git -c commit.gpgsign=false commit -m "shim: serialise Hemispheres applet selectors by name string"
```

---

## Task 1: Brancher

**Vendor:** `vendor/O_C-Phazerville/software/src/applets/Brancher.h`
**Category:** Clocking. Clock-gated path switcher.
**Expected applet_name():** "Brancher"
**Alphabetic enum position:** between `kAppletAttenuateOffset` and `kAppletBurst`.

- [ ] **Step 1: Probe**

```bash
./harness/scripts/probe_applet.sh Brancher
```

If failures, address per the per-applet workflow Step 4.

- [ ] **Step 2: Apply per-applet workflow Steps 1-3** (include, enum, factory, string).

The result in `HemispheresFactory.h` enum is:

```cpp
enum AppletIndex : uint8_t {
    kAppletEmpty = 0,
    kAppletAttenuateOffset,
    kAppletBrancher,
    kAppletBurst,
    kAppletCalculate,
    kAppletLogic,
    kAppletSlew,
    kAppletCount
};
```

with matching `applet_factory` table and `applet_enum_strings` ("Empty", "AttenOff", "Brancher", "Burst", "Calculate", "Logic", "Slew").

- [ ] **Step 3: Build, deploy, hardware verify** (workflow Steps 4-5).

- [ ] **Step 4: Commit** (workflow Step 6) with message `applets: add Brancher to Hemispheres enum`.

---

## Task 2: TLNeuron

**Vendor:** `vendor/O_C-Phazerville/software/src/applets/TLNeuron.h`
**Category:** Logic. Threshold logic neuron.
**Expected applet_name():** "TLNeuron"
**Alphabetic enum position:** after `kAppletSlew`.

- [ ] **Step 1: Probe**

```bash
./harness/scripts/probe_applet.sh TLNeuron
```

- [ ] **Step 2: Apply per-applet workflow.**

Enum after this task:

```cpp
kAppletEmpty, kAppletAttenuateOffset, kAppletBrancher, kAppletBurst,
kAppletCalculate, kAppletLogic, kAppletSlew, kAppletTLNeuron, kAppletCount
```

Strings: append "TLNeuron".

- [ ] **Step 3: Build, deploy, hardware verify.**

- [ ] **Step 4: Commit** `applets: add TLNeuron to Hemispheres enum`.

---

## Task 3: GateDelay

**Vendor:** `vendor/O_C-Phazerville/software/src/applets/GateDelay.h`
**Category:** Utility. Delays gate signals by a clock-multiplied interval.
**Expected applet_name():** "GateDelay"
**Alphabetic enum position:** between `kAppletCalculate` and `kAppletLogic`.

- [ ] **Step 1: Probe**

```bash
./harness/scripts/probe_applet.sh GateDelay
```

- [ ] **Step 2: Apply per-applet workflow.**

Enum:

```cpp
kAppletEmpty, kAppletAttenuateOffset, kAppletBrancher, kAppletBurst,
kAppletCalculate, kAppletGateDelay, kAppletLogic, kAppletSlew,
kAppletTLNeuron, kAppletCount
```

Strings: insert "GateDelay" between "Calculate" and "Logic".

- [ ] **Step 3: Build, deploy, hardware verify.**

- [ ] **Step 4: Commit** `applets: add GateDelay to Hemispheres enum`.

---

## Task 4 (stretch): Simple utility applets

A batch of low-complexity applets. Apply the per-applet workflow to each. Skip any that fail probe and document the failure for Tier 3.

**Candidates (insert alphabetically as each is added):**

- Binary (Logic) — gate logic, expected simple
- Button (Utility) — gate generator, expected simple
- ClkToGate (Utility) — clock-to-gate, expected simple
- Compare (Utility) — CV comparator, expected simple
- Cumulus (Modulator) — audit carefully; this is the only non-utility entry and may rely on shim helpers we have not stubbed. Probe first; if it fails, defer to Tier 3 and continue with the rest.
- GatedVCA (Utility) — gated VCA, expected simple

Process one at a time. Stop the batch early if shim refactor becomes necessary.

- [ ] **Step 1: Process Binary** (probe -> add -> build -> deploy -> verify -> commit)
- [ ] **Step 2: Process Button**
- [ ] **Step 3: Process ClkToGate**
- [ ] **Step 4: Process Compare**
- [ ] **Step 5: Process Cumulus**
- [ ] **Step 6: Process GatedVCA**

Any failures get documented in `docs/shim-additions.md` Round 7 with the specific missing symbol or header, then deferred to a future plan.

---

## Task 5: Documentation (Round 7)

**Files:**
- Modify: `docs/shim-additions.md`

- [ ] **Step 1: Measure final state**

Capture exact numbers BEFORE writing Round 7:

```bash
arm-none-eabi-size build/arm/Hemispheres.o
# Note text/data/bss columns and file size.

# kMaxAppletSize / kMaxAppletAlign current values: grep them.
grep "kMaxApplet" shim/include/HemispheresFactory.h
```

For each applet added, note: vendor `applet_name()`, category from comment header in `hemisphere_config.h`, any per-applet notes (icon additions, edge cases observed on hardware).

For each deferred applet, note the exact missing symbol or header path from the failed probe output.

- [ ] **Step 2: Append Round 7 section using the captured numbers**

Insert before `## Observations`. Replace each placeholder below with the captured value; do not commit unfilled placeholders.

```markdown
## Round 7 (Plan G Tier 2 applet expansion)

Expanded the `Hemispheres` applet enum from 6 to <N> entries. No shim refactor required; each applet added via three lines in `HemispheresFactory.h` (include, enum entry, factory table entry) plus one enum string. Selector serialisation switched to applet-name strings so future enum reorderings do not break saved presets.

| Applet | Category | Notes |
|--------|----------|-------|
| Brancher | Clocking | <per-applet notes from hardware verify> |
| TLNeuron | Logic | <per-applet notes> |
| GateDelay | Utility | <per-applet notes> |
| <stretch entries as they land> | | |

### Applets deferred to Tier 3

<list each probe failure with the specific missing symbol or header path; one row per deferred applet>

### Plug-in size impact

| Metric | Plan F | Plan G |
|--------|--------|--------|
| `Hemispheres.o` size | 53 KB | <measured KB> |
| `kMaxAppletSize` | <Plan F bytes> | <Plan G bytes> |
| `kMaxAppletAlign` | <Plan F bytes> | <Plan G bytes> |
```

- [ ] **Step 2: Lint**

```bash
markdownlint docs/shim-additions.md
```

- [ ] **Step 3: Commit**

```bash
git add docs/shim-additions.md
git -c commit.gpgsign=false commit -m "docs: shim-additions Round 7 documents Tier 2 applet expansion"
```

---

## Out of scope

- MIDI applets (hMIDIIn, hMIDIOut, MidiLoop) — no MIDI subsystem stubbed
- Big sequencers (Carpeggio, EnigmaJr, DrumMap, EnvSeq) — likely require HSApplication base + heavy state
- Calibr8 — calibration subsystem unavailable on NT
- ClockSetup* — extends HSClockManager beyond our stub
- GameOfLife — possibly needs 2D state buffer

These remain candidates for a future Tier 3 plan that grows the shim's HSApplication / HSClockManager support.

---

## Risk register

- **Probe false-positive:** `probe_applet.sh` only instantiates the class; it does not exercise `Controller()`/`View()`. A shim stub may exist as a forward declaration without a working body. Hardware verify catches this.
- **`kMaxAppletSize` inflation:** A single fat Tier 2 applet doubles every Hemispheres slot's sram. Audit `sizeof(Klass)` if probe shows >512 bytes; if so, raise this in review before committing.
- **Header banner clipping:** A Tier 2 applet's `View()` may draw at y=0..12. If header collides, the applet needs a per-applet workaround (NT-side render overlay) or gets deferred.
- **Encoder/button conflicts:** Some Phazerville applets use `AuxButton()` for second-button semantics. Our shim currently routes only `OnButtonPress()` to L/R encoder buttons. AuxButton is unreachable; document if any Tier 2 applet relies on it.
- **`select_mode` or other UI-state globals:** Phazerville's selection screen uses HS-level state we don't have. Applets that query it may behave oddly. Hardware verify exposes it.
