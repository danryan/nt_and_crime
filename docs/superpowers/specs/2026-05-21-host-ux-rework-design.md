# Design: per-applet host UX rework

Date: 2026-05-21
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-21-host-ux-rework-brainstorm.md`
Kickoff: `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Goal

The host plug-in's algo view becomes the user's full operating surface for a Hemispheres or Quadrants composition. The user picks slots by name (enum), reads and edits each watched slot's bus mappings and applet-specific parameters from the host's own parameter pages, and never has to leave the host page to mutate a watched applet's state.

## Acceptance criteria

- Host parameter "Slot N" presents an enum of Hemi-prefix algorithms in the current preset, by name. Value 0 means "unbound".
- Adding or removing a Hemi-prefix algorithm in the preset refreshes the enum strings on the next `draw()` cycle; existing selection by slot index is preserved across the refresh (or invalidated to "unbound" if its target disappeared).
- Each watched slot's `_NT_parameter[]` entries appear on the host's parameter pages, name-prefixed `S0:` ... `S3:`.
- Editing a proxy parameter on the host updates the slot's parameter via `NT_setParameterFromUi`; the watched applet sees the change on its next `step()`/`draw()`/`parameterChanged`.
- Editing a watched applet's parameter directly on its own algo page (not via the host) updates the host's mirrored value at the next host `draw()` so the host display stays consistent.
- Reentrancy result documented in kickoff doc. If synchronous, the proxy forward uses an explicit re-entry guard; if deferred or absent, no guard is required.
- `make test-applets` passes (existing applet tests) and a new `harness/tests/test_host_proxy.cpp` covers selector + proxy round-trip.
- ARM `.text` budget per host `.o` stays under the 82KB scan-time cap (current Hemispheres host is ~6KB; budget headroom is large).

## Architecture overview

```
                              host parameter table (s_params)
                              ┌──────────────────────────────────────────────┐
                              │ [0..K-1]  Slot enum selectors (K = numSlots) │
                              │ [K..]     Proxy params for each watched slot │
                              └──────────────────────────────────────────────┘
                                              ▲
                                              │ host->v[]
                                              │
   user encoder ──► firmware ──► parameterChanged(host_p)
                                              │
                                              ├── if host_p < K (slot enum)
                                              │     refresh enum strings via
                                              │     NT_updateParameterDefinition;
                                              │     reaggregate proxy params for
                                              │     the changed slot via
                                              │     NT_getSlot + NT_updateParameterDefinition
                                              │
                                              └── if host_p >= K (proxy)
                                                    decode (slot_idx, slot_p) from layout map
                                                    NT_setParameterFromUi(slot_idx, slot_p, v[host_p])
                                                    (re-entry guard if probe says sync)
```

## Canonical recipe (one host)

### File budget

- `plugins/hosts/<Host>_host.cpp` — host plug-in, edited in place.
- `shim/include/host_proxy.h` — header with the shared proxy aggregator state struct + helpers; declarations only.
- `shim/src/host_proxy.cpp` — implementation of helpers (kept out of header so host TUs do not multiply-define).
- `harness/tests/test_host_proxy.cpp` — Catch2 unit tests for selector + proxy round-trip.

No vendor edits. No applet `.cpp` edits. No changes to `HemiPluginInterface`.

### Shared proxy state

`shim/include/host_proxy.h` defines:

```cpp
namespace host_proxy {

constexpr int kMaxSlotsPerHost          = 4;
constexpr int kMaxProxyParamsPerSlot    = 16;
constexpr int kMaxHemiPrefixEnumEntries = 32;  // preset max slots + "unbound"

struct EnumStrings {
    char  storage[kMaxHemiPrefixEnumEntries][20];
    const char* table[kMaxHemiPrefixEnumEntries];
    int    count;
};

struct ProxyMap {
    uint32_t slot_idx;       // resolved preset slot index for this watched slot
    int      slot_param_off; // offset into slot's _NT_parameter[] for first proxy param
    int      slot_param_cnt; // number of proxy params actually exposed (<= kMax...)
};

struct State {
    int            num_slots;                          // K
    EnumStrings    enum_strs;                          // shared by all K selectors
    _NT_parameter  proxy_params[kMaxSlotsPerHost * kMaxProxyParamsPerSlot];
    char           proxy_names[kMaxSlotsPerHost * kMaxProxyParamsPerSlot][24];
    ProxyMap       maps[kMaxSlotsPerHost];
    int            kNumSlotIndexParams;                // K, copied for readers
    uint32_t       draw_count;                         // increments per draw_impl entry
};

// Build/refresh the enum string table by scanning NT_algorithmCount() slots
// for guid prefix 'Hm'. Caller passes a State to update in place.
void refresh_enum_strings(State& s);

// For a given selector enum value (0 = unbound, 1..N maps to s.enum_strs entries),
// return the actual preset slot index, or kInvalidSlotIdx if unbound.
uint32_t resolve_enum_to_slot(const State& s, int enum_value);

// Aggregate one watched slot's _NT_parameter[] entries into s.proxy_params /
// s.proxy_names at the canonical offset for `slot_lane` (0..K-1).
// slot_idx of kInvalidSlotIdx clears that lane.
void aggregate_slot(State& s, int slot_lane, uint32_t slot_idx);

// Given a host parameter index `host_p`, return the (slot_idx, slot_param_idx)
// to forward to, or {kInvalidSlotIdx, -1} when host_p is a selector or out of range.
struct ForwardTarget { uint32_t slot_idx; int slot_param_idx; };
ForwardTarget decode_forward(const State& s, int host_p);

}  // namespace host_proxy
```

`kInvalidSlotIdx = 0xFFFFFFFFu`.

`State` is sized to handle Quadrants (4*16 = 64 proxy params + 4 selectors = 68 params). Hemispheres uses only the first 2 lanes.

### Host plug-in changes

A host's static `_NT_parameter s_params[]` becomes the address of `State::proxy_params[]` instead of a static const array. The instance owns a `host_proxy::State state` inline.

`calculateRequirements_impl` sets:

```cpp
req.numParameters = kNumSlotIndexParams + kMaxSlotsPerHost * kMaxProxyParamsPerSlot;
req.sram          = sizeof(_HHInstance);  // (or _QQInstance) which now includes State
```

`construct_impl`:

1. Initialize `State` (zero all proxy params, set `kNumSlotIndexParams = K`).
2. Populate the `K` selector entries: `state.proxy_params[i] = { "Slot N", 0, 0, 0, kNT_unitEnum, kNT_scalingNone, state.enum_strs.table }`. min/max set after first enum refresh.
3. `host_proxy::refresh_enum_strings(state)`.
4. For each lane `i` in `[0, K)`, set the default enum value: matching prior behavior, default `v[i] = i + 1` so first preset Hemi slot binds to lane 0 etc. (clamped to enum count).
5. For each lane, `aggregate_slot(state, i, resolve_enum_to_slot(state, v[i]))`. (Note: `v[]` is not yet bound at construct on hardware; the helper is reentrant against the case where slot lookups return empty.)
6. Set `inst->parameters = state.proxy_params`.

`parameterChanged_impl(self, host_p)`:

```cpp
auto* inst = static_cast<_HHInstance*>(self);
auto& s    = inst->state;

if (host_p < s.kNumSlotIndexParams) {
    int lane = host_p;
    host_proxy::refresh_enum_strings(s);
    NT_updateParameterDefinition(NT_algorithmIndex(self), host_p);
    uint32_t new_slot = host_proxy::resolve_enum_to_slot(s, inst->v[host_p]);
    host_proxy::aggregate_slot(s, lane, new_slot);
    int base = s.kNumSlotIndexParams + lane * kMaxProxyParamsPerSlot;
    for (int p = 0; p < kMaxProxyParamsPerSlot; ++p) {
        NT_updateParameterDefinition(NT_algorithmIndex(self), base + p);
    }
    return;
}

// Construct-time guard: firmware fires parameterChanged for each param
// during the algorithm's construct path, before the algorithm is fully
// registered. Forwarding via NT_setParameterFromUi at that moment hard-
// crashes the device (verified via reentrancy_probe). Only forward once
// at least one draw() has run.
if (s.draw_count == 0) return;
auto t = host_proxy::decode_forward(s, host_p);
if (t.slot_idx == host_proxy::kInvalidSlotIdx) return;
NT_setParameterFromUi(t.slot_idx, t.slot_param_idx, inst->v[host_p]);
// No re-entry guard: probe confirmed NEST == 0 (firmware defers the
// downstream parameterChanged to a later frame).
```

`draw_impl` continues to resolve cached slot pointers as before. After resolve, it also calls `host_proxy::refresh_enum_strings(s)` opportunistically once per draw to catch preset edits between draws; if the enum string set changed it issues `NT_updateParameterDefinition` for each selector. (Cheap — `refresh_enum_strings` is bounded by `NT_algorithmCount`, typically 4-8.)

### Selector layout

Selector parameter struct:

```cpp
_NT_parameter sel = {
    .name    = "Slot 0",          // ...Slot 1, Slot 2, Slot 3
    .min     = 0,
    .max     = state.enum_strs.count - 1,
    .def     = 1 + lane,          // index into enum table (0 = unbound, 1.. = entries)
    .unit    = kNT_unitEnum,
    .scaling = kNT_scalingNone,
    .enumStrings = state.enum_strs.table,
};
```

Enum table entry 0 is `"---"` (unbound). Entries 1..N are the Hemi-prefix algorithms in preset order, formatted `"%d %s"` (e.g. `"3 Cumulus"`).

### Proxy name layout

Proxy parameter `_NT_parameter::name` points into `State::proxy_names[i]` formatted `"S%d %s"`:

```
S0 Trigger        <- aggregated from watched slot at lane 0, param 0
S0 Length
S0 Threshold
...               <- lane 0 fills params [K .. K+15]
S1 ...
S2 ...
S3 ...
```

24-char buffer per name covers the `S0` lane prefix (3 chars with trailing space) plus 19 chars of vendor name plus NUL.

Vendor names longer than 19 chars are truncated. The largest vendor param name observed in the codebase is `"Quantizer divisor"` (17 chars); 19 is safe.

### Re-entry guard

State carries `bool in_forward`. The proxy `parameterChanged` sets it before `NT_setParameterFromUi` and clears it after. If firmware re-enters the same host's `parameterChanged` synchronously, the guard drops the recursive call.

This guard is correct in both regimes: under deferred firmware, the `in_forward = false` happens after a no-op; under synchronous, the guard prevents stack growth.

### Spec for the reentrancy probe (Task 1)

`plugins/probes/reentrancy_probe.cpp`:

```cpp
// Two parameters: "A" and "B".
// On parameterChanged(0): NT_setParameterFromUi(self_slot, 1, v[0] + 1);
// On parameterChanged(1): no-op other than recording.
// State counters:
//   pc0_calls, pc1_calls, pc1_during_pc0 (incremented if pc1 fires while pc0 in flight).
// draw() renders "PC0=%d PC1=%d NEST=%d" so Dan can read it on hardware.
// hasCustomUi exposes encoder L which increments v[0] each click; firmware
// schedules parameterChanged(0). Dan turns the encoder once and reads the
// screen on the next refresh.
```

Hardware contract: `pc1_during_pc0 > 0` means synchronous reentry; `pc1_during_pc0 == 0 && pc1_calls > 0` means deferred (probably after the next step()).

## Test plan (host unit tests)

`harness/tests/test_host_proxy.cpp` exercises `host_proxy::` helpers without firmware. Tests:

- `enum_strings_builds_from_preset_scan` — given a mock `NT_algorithmCount`/`NT_getSlot` stub, refresh fills `enum_strs` with one entry per Hemi-prefix algorithm.
- `enum_resolves_to_slot_index` — entry 0 returns `kInvalidSlotIdx`; entry 1 returns the first matching preset slot index.
- `aggregate_slot_copies_params` — given a stub `_NT_slot::parameterInfo`, lane 0 fills `proxy_params[K..K+N]` with the vendor parameter (min/max/def/unit) and `proxy_names[K..K+N]` with `"S0 <name>"`.
- `aggregate_slot_clamps_to_kmax` — vendor slot exposing 20 params truncates to 16.
- `aggregate_slot_unbound_clears_lane` — calling with `kInvalidSlotIdx` zeroes the proxy params and names in that lane.
- `decode_forward_returns_target` — host_p = K + 3 maps to (slot_idx, 3) given the stored ProxyMap.
- `decode_forward_returns_invalid_for_selector` — host_p < K returns invalid.
- `reentry_guard_drops_recursive_forward` — call `parameterChanged_impl` while `in_forward == true` invokes no firmware call (verified via mock counter).

Test scaffolding will need shims for `NT_algorithmCount`, `NT_getSlot`, `NT_setParameterFromUi`, `NT_updateParameterDefinition`, `NT_algorithmIndex`. These are linked into the host test binary, not the device build. Reuse the existing `NT_HEM_HOST_SIM` seam.

## Hemispheres host specifics

- `kNumSlotIndexParams = 2`, lanes 0..1, total params = 2 + 16*2 = 34.
- `_HHInstance` grows to hold `host_proxy::State state` plus existing `cached_slot[2]`.
- Selectors named `"Slot 0"`, `"Slot 1"`.
- Default enum values on `construct`: 1 (slot 0 of the preset's Hemi entries) and 2 (slot 1).

## Quadrants host specifics

- `kNumSlotIndexParams = 4`, lanes 0..3, total params = 4 + 16*4 = 68.
- `_QQInstance` already has `parameterChanged_impl` stub at line 198; this work replaces the body. `focused_slot_idx` and serialise/deserialise stay.
- Selectors named `"Slot 0"`..`"Slot 3"`.
- Default enum values on `construct`: 1, 2, 3, 4 (first four Hemi entries).

## Hardware smoke checks

1. Load Hemispheres Host into preset slot 0; load two Hemi per-applet plug-ins into slots 1-2.
2. Visit host algo page; verify "Slot 0" enum lists slot 1's applet name, "Slot 1" lists slot 2's.
3. Scroll a proxy parameter (e.g. `S0 Threshold`); verify the watched applet's algo page reflects the new value.
4. Add a third Hemi plug-in to the preset; return to host page; verify the enum list grew.
5. Repeat for Quadrants Host with 4 watched slots.

## Spec footer

### Recipe spot-check

Walk one host (Hemispheres) end-to-end against the recipe: 8 sub-steps in `construct_impl`, 3 sub-steps in `parameterChanged_impl` selector branch, 4 sub-steps in `parameterChanged_impl` proxy branch. All sub-steps reference symbols in `host_proxy::` namespace defined in the header. No vendor edits referenced.

### Per-entry verification

Verify each per-host entry exists in this spec:

- Hemispheres specifics: present (line "Hemispheres host specifics").
- Quadrants specifics: present (line "Quadrants host specifics").

### Shim prereq verification

Required additions to `shim/`:

- `shim/include/host_proxy.h` — new header.
- `shim/src/host_proxy.cpp` — new implementation.

Verify the existing `shim/include/host_helpers.h` does not conflict: it defines `host_helpers::ResolvedSlot` and helpers; no symbol clash with `host_proxy::`. Two separate namespaces; safe.

Verify ARM `.text` impact is acceptable: helper functions are small (refresh: ~50 lines, aggregate: ~30 lines, decode: ~10 lines). `arm-none-eabi-readelf -W -S` on the resulting host `.o` must show `.text` under 82KB. Current Hemispheres_host.o is ~6KB; budget is far from binding.

Verify diagnostic probe location matches CLAUDE.md convention: `plugins/probes/reentrancy_probe.cpp`. Probe is non-deployable to a normal preset slot in any non-diagnostic context; existing probes (`aeabi_probe`, `section_probe`) live here.
