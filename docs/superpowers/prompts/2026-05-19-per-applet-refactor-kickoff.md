# Per-applet plug-in refactor (autonomous, multi-release)

This kickoff prompt is the durable handoff for restructuring the Hemisphere
applet distribution. Today every applet is statically linked into a single
bundled NT plug-in (`Hemispheres.o` primary plus `Hemispheres2.o` secondary)
hosted by `applets/Hemispheres.cpp`. The bundled `.text` keeps pressing the
firmware's ~82 KB per-`.o` scan-time cap. PR #10 (squash `75ba7ab`) shrank
the bundle by flag tuning plus Lorenz dep cleanup but did not change the
bundling shape.

This refactor unbundles the 56 applets into one NT plug-in per applet and
introduces two thin host plug-ins (`Hemispheres`, `Quadrants`) that compose
per-applet plug-ins via the firmware's slot API plus a function-pointer-in-data
interface stored on each per-applet algorithm instance. No applet code is
statically linked into either host. Per-applet plug-ins are 16-20 KB `.text`
each, well under the cap. Runtime ITC scales with what the user actually
loads, not the bundled set.

## Release sequence

Three releases. Each ships through a PR cycle.

- **Pilot release** (this kickoff): 6 pilot per-applet plug-ins plus both
  host plug-ins. Validates the per-applet pattern, the function-pointer ABI,
  the host control-claim model, and the .text and ITC consumption estimates.
  The bundled `Hemispheres.o` plus `Hemispheres2.o` continue to ship so
  existing presets keep working.
- **Mass-port release**: 50 remaining per-applet plug-ins (parallel
  implementer fan-out per `~/.claude/rules/parallel-execution.md`). Bundled
  artifacts continue to ship as the user transition path.
- **Cleanup release**: bundled `Hemispheres.o` plus `Hemispheres2.o` plus
  their supporting shim and Makefile machinery removed. Authored after a
  one-release transition window post mass-port so users have time to
  migrate presets before the bundled format is gone.

The pilot release is the only release this kickoff executes end-to-end. The
mass-port release and cleanup release get their own kickoff prompts authored
at the end of the preceding release.

## The two most important rules

These are repeated because the Phase 3 attempt 1 retrospective traces the
entire failure to violating them:

1. **PARALLELIZE INDEPENDENT WORK.** Default to parallel. The 6 per-applet
   pilots are independent at the file-surface level. The plan must dispatch
   them as parallel implementer subagents in a single message (multiple
   Agent tool calls in one message). End-to-end wallclock target is roughly
   the time of the slowest single pilot plus setup prep plus integration.
   The two hosts can also be dispatched in parallel after the
   HemiPluginInterface plus host helpers (setup commits) are committed.
2. **EVERY IMPLEMENTER WORKTREE BRANCHES FROM `dr/per-applet-pilot`, NOT
   FROM `main`.** The parent agent must specify the base branch explicitly:
   `git worktree add <path> -b <implementer-branch> dr/per-applet-pilot`.
   Never rely on the default.

## Model selection

- Parent orchestrator: `opus` (this is what you are).
- Implementer subagents: `sonnet`.
- Audit and Explore subagents: `sonnet`.

## Autonomous execution contract

The agent runs through these steps without pausing for user review except at
the preflight checkpoint. Soft wallclock ceiling: 4 hours from preflight to
PR open. If the parent agent exceeds this, pause and reassess rather than
churning.

1. Audit and verification:
   - Read primary references.
   - Verify rule prerequisites.
   - Audit the per-applet template against the pilot applets.
   - Confirm vendor dep accounting per pilot (see "Vendor dep accounting"
     section below for the rule).
   - Draft the HemiPluginInterface layout.
   - Confirm NT_MULTICHAR byte order. `vendor/distingNT_API/include/distingnt/api.h:120`
     defines `NT_MULTICHAR(a,b,c,d) = ((a<<0) | (b<<8) | (c<<16) | (d<<24))`,
     so the guid prefix filter `(guid & 0xFFFF) == kHemiGuidPrefix` checks
     bytes 0 and 1 (the `a` and `b` chars) which is what the convention
     requires. This audit step exists so a future API change is caught
     before the filter silently breaks.
   - Probe-deploy a minimal `hasCustomUi` claim plug-in (extend
     `applets/aeabi_probe.cpp` or write a fresh diagnostic) and confirm on
     hardware that the firmware actually honors the claim by suppressing
     default encoder behavior while the probe is focused. Header-only
     inspection is not sufficient.
2. Preflight checkpoint (post a one-paragraph preflight report, proceed
   unless an abort condition fired during audit).
3. Pilot brainstorm doc
   (`docs/superpowers/brainstorms/2026-05-19-per-applet-pilot-brainstorm.md`).
4. Pilot spec doc
   (`docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md`).
5. Pilot plan doc
   (`docs/superpowers/plans/2026-05-19-per-applet-pilot-plan.md`).
6. Setup commits (parent agent, sequential, land on the feature branch):
   HemiPluginInterface header, host_helpers header plus implementation,
   applet_manifest schema header, per-applet runtime helpers, applet-side
   hasCustomUi and customUi defaults, Makefile rules for per-applet plus
   hosts, pre-commit hook, section markers in tests, directory restructure
   (`plugins/applets/`, `plugins/hosts/`, `plugins/probes/`).
7. Per-applet implementer fan-out (parallel implementer subagents): 6
   pilots dispatched in a single message: applet-compare,
   applet-clockdivider, applet-vectorlfo, applet-cumulus, applet-relabi,
   applet-probabilitydivider.
8. Host implementer fan-out (parallel implementer subagents): 2 hosts
   dispatched in a single message after pilot commits land:
   host-hemispheres, host-quadrants. Hosts depend on the pilots existing as
   compilable per-applet `.o` files so the cross-slot routing has real
   targets to test against.
9. Integration on the feature branch. Cherry-pick implementer commits.
   Verify `make arm` clean and `make test-applets` clean. Build the actual
   `.o` artifacts. Run `arm-none-eabi-size` on all 8 new `.o` files (6
   applets plus 2 hosts) and publish the table.
10. Hardware smoke check. Deploy each pilot standalone via sysex; confirm
    registration. Deploy Hemispheres host plus 2 pilot applets in the same
    preset; exercise both encoders plus button1 and button2. Deploy
    Quadrants host plus 4 pilot applets in the same preset; exercise direct
    slot select via button1-4 plus the L and R encoders. Capture NT View
    Info screen for each configuration; publish the ITC consumption table.
11. Manifest schema freeze checkpoint. After integration plus hardware
    verification, post a confirmation that the manifest schema is frozen.
    Schema changes from the mass-port release onward require explicit
    replan.
12. PR open with Load-bearing decisions section. Body includes the
    per-applet text size table, the ITC consumption table, and the
    manifest schema-frozen confirmation.

## Branch context

The pilot release runs on the new feature branch `dr/per-applet-pilot`.
Create the worktree at `.worktrees/per-applet-pilot`. Branch from current
`main` at `75ba7ab`.

Initialize submodules in the new worktree immediately after creation:

```sh
git -C .worktrees/per-applet-pilot submodule update --init --recursive --depth=1
```

## Worktree naming

- Parent worktree: `.worktrees/per-applet-pilot` on branch
  `dr/per-applet-pilot`.
- Implementer worktrees: `.worktrees/per-applet-<role>-<slug>` on branch
  `per-applet-<role>/<slug>` where role is `applet` or `host`.
- Setup commits plus integration commits land directly on
  `dr/per-applet-pilot`.

## Pre-commit hook

Reuse the previous hook template at `.git/hooks/pre-commit`. Update
branch-name patterns to accept commits on:

- `dr/per-applet-pilot` (setup commits, integration).
- `per-applet-applet/*` (per-applet pilot implementers).
- `per-applet-host/*` (host plug-in implementers).

Reject commits on `main`, on any branch not derived from
`dr/per-applet-pilot`, and any `per-applet-applet/*` or `per-applet-host/*`
commit that stages a file outside the implementer's allowed surface. The
allowed surface is per-implementer (each implementer prompt names its
files); the hook enforces a coarse rule:

- `per-applet-applet/<slug>` may only touch `plugins/applets/<slug>.cpp`,
  `shim/include/applet_manifests/<slug>.h`, and
  `harness/tests/test_applet_<slug>.cpp`.
- `per-applet-host/<slug>` may only touch `plugins/hosts/<slug>.cpp`,
  `harness/tests/test_host_<slug>.cpp`, and Makefile fragments for the host.

Hard-reject rule on `per-applet-applet/*` and `per-applet-host/*` branches:
reject any commit that stages `shim/include/HemiPluginInterface.h`,
`shim/include/applet_manifest.h`, `shim/include/host_helpers.h`,
`shim/src/host_helpers.cpp`, `plugins/applets/_per_applet_runtime.h`,
`harness/tests/test_hemispheres.cpp`, or any file outside `plugins/` and
the implementer's own manifest plus test files. Those setup files are
owned by the parent agent on the feature branch; implementers must never
touch them.

## Required skills and rules

- `superpowers:using-git-worktrees`. Step zero.
- `superpowers:brainstorming`. Drives the brainstorm doc generation.
- `superpowers:writing-plans`. Drives the spec plus plan doc generation.
- `superpowers:subagent-driven-development`. Drives implementer fan-out
  dispatch.

Load and honor `~/.claude/rules/parallel-execution.md`. Inline the
worktree-dispatch checklist into the plan so it is auditable without the
personal rules file.

## Primary references (read these before starting)

- `CLAUDE.md` (the in-repo CLAUDE.md, full read).
- `vendor/distingNT_API/include/distingnt/api.h` (focus: `_NT_factory`,
  `_NT_slot`, `NT_getSlot`, `NT_setParameterFromAudio`, `hasCustomUi`,
  `customUi`, `_NT_uiData`, `_NT_controls`).
- `vendor/distingNT_API/include/distingnt/slot.h` (full).
- `vendor/distingNT_API/examples/explore.cpp` (cross-slot read pattern).
- `vendor/distingNT_API/examples/multiple.cpp` (multi-factory plug-in
  pattern).
- `vendor/distingNT_API/examples/flexSeqSwitch.cpp` (control claim model).
- `applets/Hemispheres.cpp` (current bundled host - replaced by the two new
  hosts).
- `shim/include/HemispheresFactory.h` (current factory table - replaced by
  per-applet `.o` files).
- `applets/solo_probe.cpp` plus `applets/aeabi_probe.cpp` (existing
  per-applet measurement diagnostic plus ABI probe).
- PR #10 (squash `75ba7ab`) for the recent flag tuning context.

## Architecture (the high-level shape)

After the pilot release lands, the build produces:

| Artifact | Count | Path | Approx .text |
| --- | --- | --- | --- |
| Per-applet pilot plug-ins | 6 | `plugins/applets/<APPLET>.cpp` | 16-20 KB each |
| Hemispheres host (2 slots, 64x64) | 1 | `plugins/hosts/Hemispheres.cpp` | ~16 KB |
| Quadrants host (4 slots, 64x64) | 1 | `plugins/hosts/Quadrants.cpp` | ~16 KB |
| Bundled (retained for transition) | 2 | `applets/Hemispheres.cpp`, `applets/Hemispheres2.cpp` | 78-64 KB |
| Diagnostic probes | 4 | `plugins/probes/` | (existing) |

The function-pointer-in-data interface bridges hosts and per-applet
plug-ins. Function pointers are data, not symbols requiring load-time
resolution; the NT firmware loader does not see them at all. The host calls
them via an indirect call into the applet's `.text` which is already mapped
in ITC from its own registration. No cross-`.o` symbol resolution is
required.

## HemiPluginInterface ABI (versioned)

Defined in `shim/include/HemiPluginInterface.h`:

```cpp
#pragma once
#include <distingnt/api.h>
#include <cstdint>

constexpr uint32_t kHemiInterfaceMagic   = NT_MULTICHAR('H','M','I','1');
constexpr uint32_t kHemiInterfaceVersion = 1;

struct HemiPluginInterface : public _NT_algorithm {
    uint32_t magic;                                                          // must equal kHemiInterfaceMagic
    uint32_t interface_version;                                              // must be >= kHemiInterfaceVersion
    void (*render_view)(_NT_algorithm* self, int origin_x, int origin_y);
    void (*on_encoder_turn)(_NT_algorithm* self, int direction);
    void (*on_encoder_turn_shifted)(_NT_algorithm* self, int direction);     // for Quadrants R encoder; default = on_encoder_turn
    void (*on_button_press)(_NT_algorithm* self);
    void (*on_aux_button)(_NT_algorithm* self);
};
```

Per-applet `construct()` populates the magic plus version plus function
pointers. Template default for the shifted handler:

```cpp
inst->on_encoder_turn_shifted = inst->on_encoder_turn;  // default; applet may override below
```

Hosts validate before calling through: guid prefix check, then magic check,
then version check. Mismatches render the incompatible stub (see below).

## Guid prefix convention

Every Hemi-family plug-in uses the 2-char prefix `Hm` as the first two
bytes of its 4-character NT guid. Example: `ADEG` becomes
`NT_MULTICHAR('H','m','A','d')` rendered as `"HmAd"` in JSON. The hosts'
guid filter is:

```cpp
constexpr uint32_t kHemiGuidPrefix = NT_MULTICHAR('H','m',0,0) & 0xFFFF;

bool is_hemi_plugin(uint32_t guid) {
    return (guid & 0xFFFF) == kHemiGuidPrefix;
}
```

Each per-applet manifest header carries a `static_assert` enforcing the
prefix:

```cpp
static_assert((kAppletGuid & 0xFFFF) == kHemiGuidPrefix,
              "Hemi applet guid must start with 'Hm'");
```

Build-time enforcement prevents accidental collisions across 56 manifests.

## Incompatible plug-in stub

Defined in `shim/include/host_helpers.h` and rendered from
`shim/src/host_helpers.cpp`. When a host's slot validation fails (no
plugin, non-Hemi guid, ABI magic mismatch, version too low), the host
calls:

```cpp
void render_incompatible_stub(int origin_x, int origin_y);
```

Renders a 64x64 box outline (1-pixel border) with `"INCOMPATIBLE"` text
centered horizontally at y = origin_y + 30. Same routine for both hosts so
the visual is consistent. Quadrant focus border (when applicable) still
draws around the stub.

### Host helper slot-resolution caching

Hosts MUST cache the slot-resolution result per draw cycle. The expensive
parts are `NT_getSlot`, the guid-prefix filter, the cast to
`HemiPluginInterface*`, and the magic + version validation. `slot.guid()`
is called per draw pass and should not be re-queried per encoder event
inside the same draw. `shim/include/host_helpers.h` exposes:

```cpp
struct ResolvedSlot {
    HemiPluginInterface* plugin;  // nullptr if the slot did not validate
    uint32_t             guid;
};

ResolvedSlot resolve_slot(uint32_t slot_idx);
```

Host calls `resolve_slot()` once per slot per draw or event; routes
encoder/button events to `resolved.plugin->on_encoder_turn(...)` etc. if
`plugin` is non-null, otherwise renders the incompatible stub.

## NT control claim model

Per-applet plug-in standalone control claim:

```cpp
uint32_t hasCustomUi(_NT_algorithm* self) {
    return kNT_encoderL | kNT_encoderButtonL | kNT_button1;
}
```

In `customUi()`, route via the same HemiPluginInterface function pointers
the host would use (so the applet's behavior is identical standalone and
hosted):

| NT control | Action |
| --- | --- |
| `uiData.encoders[0]` (L encoder turn) | `on_encoder_turn(self, direction)` |
| `kNT_encoderButtonL` edge (XOR `lastButtons`) | `on_button_press(self)` |
| `kNT_button1` edge | `on_aux_button(self)` |

Hemispheres host claim: `kNT_encoderL | kNT_encoderR | kNT_encoderButtonL |
kNT_encoderButtonR | kNT_button1 | kNT_button2`.

| NT control | Action |
| --- | --- |
| L encoder turn | Slot 1 `on_encoder_turn(direction)` |
| R encoder turn | Slot 2 `on_encoder_turn(direction)` |
| L encoder button | Slot 1 `on_button_press` |
| R encoder button | Slot 2 `on_button_press` |
| button1 | Slot 1 `on_aux_button` |
| button2 | Slot 2 `on_aux_button` |

Quadrants host claim: `kNT_encoderL | kNT_encoderR | kNT_encoderButtonL |
kNT_encoderButtonR | kNT_button1 | kNT_button2 | kNT_button3 | kNT_button4`.

| NT control | Action |
| --- | --- |
| button1-4 edge | Set focused slot to 1/2/3/4 (direct select; no cycle) |
| L encoder turn | Focused slot `on_encoder_turn(direction)` |
| R encoder turn | Focused slot `on_encoder_turn_shifted(direction)` |
| L encoder button | Focused slot `on_button_press` |
| R encoder button | Focused slot `on_aux_button` |

Focused quadrant rendered with inverted 64x64 border.

## Vendor dep accounting

Each per-applet `.o` partial-links only the vendor object files the applet
actually references. The Makefile per-applet macro takes a per-applet
`VENDOR_DEPS_<applet>` variable. Examples:

```makefile
VENDOR_DEPS_VectorLFO         := build/arm/shim_src/lorenz/streams_resources.o build/arm/shim_src/lorenz/streams_lorenz_generator.o
VENDOR_DEPS_VectorEG          := build/arm/shim_src/lorenz/streams_resources.o build/arm/shim_src/lorenz/streams_lorenz_generator.o
VENDOR_DEPS_Compare           :=
VENDOR_DEPS_ClockDivider      :=
VENDOR_DEPS_Cumulus           :=
VENDOR_DEPS_Relabi            := build/arm/shim_src/lorenz/streams_resources.o build/arm/shim_src/lorenz/streams_lorenz_generator.o
VENDOR_DEPS_ProbabilityDivider :=
```

The applet manifest header records its vendor dep list as a comment for
human review:

```cpp
// Vendor deps: streams_resources.o, streams_lorenz_generator.o
namespace per_applet::VectorLFO { ... }
```

Each implementer's audit determines the dep list by walking the applet
header's `#include` graph (transitive, until the includes reach files
already in shim baseline or the api). Empty dep lists are common and
correct.

## Per-applet manifest schema

`shim/include/applet_manifest.h` declares the shared types:

```cpp
enum class BusKind : uint8_t {
    gate,           // NT_PARAMETER_GATE pattern (binary trigger)
    cv,             // NT_PARAMETER_CV_INPUT or NT_PARAMETER_CV_OUTPUT
    audio,          // NT_PARAMETER_AUDIO_INPUT or NT_PARAMETER_AUDIO_OUTPUT
};

struct BusParam {
    const char* name;
    BusKind     kind;
};
```

`shim/include/applet_manifests/<APPLET>.h` (one per applet, generated as
part of each implementer's port):

```cpp
#pragma once
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace per_applet::ADEG {
    constexpr uint32_t guid           = NT_MULTICHAR('H','m','A','d');
    constexpr const char* name        = "ADEG";
    constexpr const char* description = "Phazerville ADSR envelope generator";
    constexpr BusParam    inputs[]    = {
        { "Gate 1", BusKind::gate },
        { "Gate 2", BusKind::gate },
    };
    constexpr BusParam    outputs[]   = {
        { "Env A", BusKind::cv },
        { "Env B", BusKind::cv },
    };
}
```

The per-applet runtime helper (`plugins/applets/_per_applet_runtime.h`,
header-included by each per-applet `.cpp`) consumes the manifest to build
the `_NT_parameter[]` table at compile time, populates the
HemiPluginInterface function pointers in `construct()`, and wires the
standalone customUi.

## Per-applet plug-in template structure

Each `plugins/applets/<APPLET>.cpp` is essentially:

```cpp
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/<APPLET>.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/<APPLET>.h"
#include "../../shim/include/hemispheres_shim.h"
#include "_per_applet_runtime.h"

namespace { using ManifestNS = per_applet::<APPLET>; }

struct _AppletInstance : public HemiPluginInterface {
    <APPLET> applet;
};

// calculateRequirements, construct, step, draw, parameterChanged, serialise,
// deserialise, hasCustomUi, customUi all live here. construct() populates
// magic plus version plus function pointers.

_NT_factory factory = {
    .guid = ManifestNS::guid,
    .name = ManifestNS::name,
    .description = ManifestNS::description,
    // ...
};

uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    // standard switch
}
```

The shared parameter-table assembly plus customUi routing live in a small
header `plugins/applets/_per_applet_runtime.h` so per-applet `.cpp` files
are mostly manifest plus applet header inclusion plus the boilerplate
above.

### Per-applet hook contract

Implementers MUST implement each `_NT_factory` hook per this table. Glue
helpers live in `_per_applet_runtime.h` so all 6 implementers produce
identical glue, not 6 variants.

| Hook | Per-applet responsibility |
| --- | --- |
| `calculateRequirements()` | `req.numParameters = sizeof(per_applet::<APPLET>::inputs)/sizeof(BusParam) + sizeof(outputs)/sizeof(BusParam) + applet-specific param count`. `req.sram = sizeof(_AppletInstance)`. Other pools 0 unless the applet's vendor deps require otherwise. |
| `construct()` | Placement-new `_AppletInstance`. Set `magic = kHemiInterfaceMagic`, `interface_version = kHemiInterfaceVersion`, populate the 4 function pointers (render_view, on_encoder_turn, on_button_press, on_aux_button). Set `on_encoder_turn_shifted = on_encoder_turn` as default. Call applet's `Start()`. |
| `step()` | Use `per_applet_runtime::populate_frame_from_bus(self, busFrames, manifest)` to populate `HS::frame.inputs`, `HS::frame.clocked`, `HS::frame.gate_high` from bus inputs per the manifest. Call applet's `Controller()` once per inner tick using `per_applet_runtime::run_controller_inner_ticks(self, numFramesBy4)`. The helper derives `ticks_this_step = numFrames / 3` (matches the bundled shim at `shim/include/hemispheres_shim.h:179`), so applets observe the same 10x-per-buffer rate that the existing bundled host already gives them; the rate is firmware-derived, not per-applet. Then call `per_applet_runtime::write_outputs_to_bus(self, busFrames, manifest)` to drain `HS::frame.outputs` to bus outputs. |
| `draw()` | `((HemiPluginInterface*)self)->render_view(self, 0, 0); return true;`. Returning `true` means firmware suppresses its standard parameter strip so the applet's 64x64 framing is consistent between standalone use and hosted use. |
| `parameterChanged()` | Forward to the applet's parameter handler via the existing shim pattern. If the applet has no parameter-change hook, no-op. |
| `serialise()` | Wrap existing Hemisphere `OnDataRequest()` via `_NT_jsonStream` per-applet packer. Use `per_applet_runtime::write_data_request(self, stream)`. |
| `deserialise()` | Wrap existing Hemisphere `OnDataReceive()` via `_NT_jsonParse` per-applet unpacker. Use `per_applet_runtime::read_data_receive(self, parse)`. |
| `midiMessage()` / `midiRealtime()` | Forward to applet MIDI handler if the applet declares one; otherwise leave the function pointer null. |
| `hasCustomUi()` | Returns the bitmask `kNT_encoderL \| kNT_encoderButtonL \| kNT_button1`. Standardized; no per-applet variation. |
| `customUi()` | Implemented in `_per_applet_runtime.h::route_custom_ui(self, uiData)`. Routes encoder + button events through the same HemiPluginInterface function pointers the host would use. Per-applet `.cpp` calls the helper; does NOT reimplement routing. |

### Per-instance overhead

Each per-applet plug-in instance carries 28 bytes of HemiPluginInterface
overhead above `_NT_algorithm`: `magic` (4) + `interface_version` (4) +
5 function pointers (5 x 4 = 20). Accepted as cheap insurance for the
versioned ABI; do not optimize this away.

## Pilot applet selection (6 pilots)

| Pilot | Why this applet |
| --- | --- |
| Compare | Smallest unique text (0 bytes per the solo probe). Baseline that the per-applet plumbing works at all. |
| ClockDivider | Typical small applet. Exercises clock plus division logic that maps to `kNT_encoderButtonL` and button1 standalone. |
| VectorLFO | Exercises vendor deps (VectorOscillator, WaveformManager, tideslite) per-applet. Confirms vendor dep accounting per `.o` works. |
| Cumulus | Exercises HS::frame writes plus the 10x ticks-per-step gotcha (per CLAUDE.md "10x clocked multiplier" section). Confirms the step() wrapper handles vendor-frame globals correctly. |
| Relabi | Fattest applet (4204 B unique per the solo-probe measurements). Validates the per-applet pattern under the worst-case text size. Exercises RelabiManager plus segment-formatted text rendering. |
| ProbabilityDivider | Exercises the ProbLoopLinker singleton (private to this `.o` after the refactor). Confirms the singleton-private-to-`.o` semantics document the gotcha if ProbabilityMelody is ever ported. |

## Decisions captured

| # | Decision |
| --- | --- |
| 1 | Three-release sequence. Pilot release: 6 pilots plus both hosts. Mass-port release: 50 remaining plus transition window. Cleanup release: bundled deletion. |
| 2 | Pilot set: Compare, ClockDivider, VectorLFO, Cumulus, Relabi, ProbabilityDivider. |
| 3 | Bus parameters per applet. Each applet's manifest declares its own minimal set. No fixed Phazerville L, R, A, or B convention at the per-applet layer. |
| 4 | Bundled `Hemispheres.o` plus `Hemispheres2.o` retained throughout the pilot release AND the mass-port release. Deleted in the cleanup release with a one-release transition window after mass-port completion. |
| 5 | After bundled deletion, old presets fail to load. No migration tooling. Transition window is the user safeguard. |
| 6 | All 56 applets in scope. ProbabilityDivider's ProbLoopLinker singleton works within its own `.o`. Documented gotcha if ProbabilityMelody is ever ported. |
| 7 | Two host source files (`plugins/hosts/Hemispheres.cpp`, `plugins/hosts/Quadrants.cpp`). Not a parameterized template. Different control schemes don't fit a single source. Shared helpers under `shim/include/host_helpers.h`. |
| 8 | NT control claim via `hasCustomUi` plus `customUi`. Per-applet claims encoderL plus encoderButtonL plus button1 standalone. Hemispheres claims both encoders plus their buttons plus button1 and button2. Quadrants claims both encoders plus their buttons plus button1-4. |
| 9 | HemiPluginInterface has `magic` ('HMI1') plus `interface_version` (starts at 1). Hosts validate before calling function pointers. Mismatched slots render the incompatible stub. |
| 10 | One host per preset is the supported configuration. Both-hosts-loaded scenario is tested during hardware verification; result determines whether it is certified or documented as unsupported. |
| 11 | `draw()` for every per-applet plug-in is a thin wrapper that delegates to `render_view(self, 0, 0)`. No duplicate rendering implementations. |
| 12 | Manifest schema for `shim/include/applet_manifests/<APPLET>.h` is frozen at the schema freeze checkpoint after the pilot release. Schema changes from the mass-port release onward require explicit replan. |
| 13 | Guid prefix `Hm` (first two bytes of the 4-char NT guid) identifies Hemi-family plug-ins. Build-time `static_assert` per manifest enforces the prefix. |
| 14 | Quadrants R encoder routes to `on_encoder_turn_shifted`. Per-applet plug-ins default it to `on_encoder_turn` in `construct()` unless the applet overrides. |
| 15 | Directory layout: `plugins/applets/`, `plugins/hosts/`, `plugins/probes/`. The top-level `applets/` directory is deprecated; bundled host files stay there until the cleanup release. Probes move to `plugins/probes/` in the setup commits. CLAUDE.md update in the setup-commits step must explicitly state: "`applets/` directory deprecated; remaining contents (`Hemispheres.cpp`, `Hemispheres2.cpp`) will move or be removed in the cleanup release." |

## Test strategy

Per-applet test (Catch2, one file per applet, mirrors current
`harness/tests/test_hemispheres.cpp` per-applet section markers):

- Round-trip via `OnDataRequest` plus `OnDataReceive` using the existing
  `pack_<applet>` helpers.
- Behavior tests through standalone bus I/O paths.
- For ProbabilityDivider: confirm the ProbLoopLinker singleton state
  survives round-trip within a single per-applet `.o` instance.

Host routing tests (Catch2, `harness/tests/test_host_hemispheres.cpp` and
`harness/tests/test_host_quadrants.cpp`):

- Happy path: host renders each pilot applet in its slot configuration;
  routes encoder plus button to the applet's HemiPluginInterface function
  pointers; verify the applet's state advances.
- ABI-mismatch test: host attempts to call into a slot whose `magic` is
  wrong (test fixture installs a fake HemiPluginInterface with `magic = 0`).
  Verify graceful no-op plus incompatible stub rendered.
- Empty-slot test: host configured with an unused slot index (slot returns
  nullptr from `slot.plugin()`). Verify no crash, slot region renders the
  stub.
- Wrong-guid test: host gets a non-Hemi algorithm in target slot (test
  fixture installs an algorithm with guid prefix other than `'Hm'`). Verify
  guid filter rejects it, stub rendered.

Integration verification table (publish in the PR body):

- Per-applet `arm-none-eabi-size` `.text` for each of the 6 pilots plus
  each of the 2 hosts. Confirms 16-20 KB estimate.
- Total `.text` for the still-shipping bundled `Hemispheres.o` plus
  `Hemispheres2.o`. Confirms the refactor did not regress the existing
  bundled artifacts.

Hardware verification:

- Each pilot deployed standalone via `make deploy-sysex` to its own free
  slot. Misc > Plug-ins > View Info should mark each as loaded with the
  per-plug-in ITC, DTC, and DRAM stats. Capture screen with
  `mcp__nt_helper__show_screen`.
- Hemispheres host plus 2 pilot applets in the same preset. Each encoder
  plus button routes to the expected slot. Capture screen.
- Quadrants host plus 4 pilot applets in the same preset. button1-4
  directly select focus. L and R encoders route to focused slot. Capture
  screen.
- Both-hosts-loaded stress test (Hemispheres plus 2 applets plus Quadrants
  plus 4 applets in the same preset). Read View Info ITC consumption. If
  under the empirical ~100 KB ceiling, certify the configuration. If over,
  document the constraint plus the failure mode.
- Publish the ITC consumption table in the PR body.

## Setup commit file inventory

The parent agent commits these files on `dr/per-applet-pilot` before
dispatching per-applet implementers:

- `shim/include/HemiPluginInterface.h` (the versioned interface struct).
- `shim/include/applet_manifest.h` (shared `BusKind` plus `BusParam`
  types).
- `shim/include/host_helpers.h` (declarations).
- `shim/src/host_helpers.cpp` (slot validation, incompatible stub
  rendering, guid filter helpers).
- `plugins/applets/_per_applet_runtime.h` (shared parameter-table
  assembly, customUi router, default function-pointer wiring).
- `plugins/applets/.gitkeep` (directory).
- `plugins/hosts/.gitkeep` (directory).
- `plugins/probes/.gitkeep` (directory; probes moved here from `applets/`).
- `Makefile` updates: per-applet rule macro, host rule macro,
  `PILOT_APPLET_LIST := Compare ClockDivider VectorLFO Cumulus Relabi
  ProbabilityDivider`, host build targets, plus updated paths for probe
  rules (existing `applets/aeabi_probe.cpp`, `applets/bus_probe.cpp`,
  `applets/section_probe.cpp`, `applets/solo_probe.cpp` references move to
  `plugins/probes/`).
- `.git/hooks/pre-commit` updates: branch name patterns plus forbidden
  surface per branch.
- `harness/tests/test_applet_<applet>.cpp` template skeleton (one per
  pilot, parent-committed before per-applet implementer fan-out so each
  implementer fills in its own file without colliding with siblings).
  Skeleton contains the Catch2 include, namespace setup, a single
  `TEST_CASE("<applet> placeholder", "[<applet>]") { /* TODO: implementer
  fills in */ }` block, and a header comment listing the per-applet
  manifest reference plus any per-applet test concerns (e.g. 10x
  ticks-per-step gotcha, singleton semantics) the implementer must
  address.

## Implementer subagent prompts

Per-applet implementer prompts (one per pilot applet) MUST include:

- Worktree path plus branch (e.g., `.worktrees/per-applet-applet-cumulus`
  on `per-applet-applet/cumulus`).
- First-action verification (`pwd`, `git rev-parse --abbrev-ref HEAD`,
  `test -f docs/superpowers/specs/2026-05-19-per-applet-pilot-design.md`).
- Submodule init command.
- Allowed surface: `plugins/applets/<APPLET>.cpp`,
  `shim/include/applet_manifests/<APPLET>.h`,
  `harness/tests/test_applet_<APPLET>.cpp`. The implementer writes a NEW
  per-applet test file (no section-marker addition to `test_hemispheres.cpp`;
  the existing bundled test file is outside the implementer's allowed
  surface per the pre-commit hook).
- Forbidden surface (enforced by pre-commit hook): everything else.
- The manifest schema reference and the `_per_applet_runtime.h` API
  surface so the implementer doesn't reinvent the parameter-table
  assembly.
- Per-applet test concerns (10x ticks-per-step gotcha for Cumulus,
  singleton semantics for ProbabilityDivider, etc.).

Host implementer prompts (one per host) MUST include the same first-action
verification, allowed surface (`plugins/hosts/<HOST>.cpp`,
`harness/tests/test_host_<HOST>.cpp`), forbidden surface, the
HemiPluginInterface API surface, and the control-claim spec.

## Abort conditions

If the audit step discovers any of the following, halt and post an abort
report under `docs/superpowers/abort-reports/`:

- Vendor `_NT_factory` does NOT actually expose `hasCustomUi` or
  `customUi` hooks in the firmware version we target. (`setupUi` and pot
  claims are pilot-out-of-scope; future iterations may need it.)
- Cross-slot `_NT_slot::plugin()` returns nullptr for our own per-applet
  plug-in instances on hardware (the function-pointer-in-data trick
  requires the pointer to be valid).
- `NT_setParameterFromAudio` does not propagate updates to other slots'
  internal state in the firmware version we target.
- The 6 pilots' combined .text exceeds 130 KB across the 6 `.o` files
  (would indicate the per-applet estimate is significantly wrong and the
  ITC scenarios need rethinking before the mass-port release).

Each abort condition would invalidate a load-bearing assumption of the
refactor.

## What the parent agent MUST do at the end

When all steps complete and the manifest schema freeze checkpoint passes,
open a PR titled "Per-applet refactor pilot release (6 applets plus 2
hosts)" with a body containing:

- Per-applet `.text` size table (6 pilots plus 2 hosts).
- ITC consumption table from hardware verification.
- Manifest schema-frozen confirmation.
- The decision list above.
- A Test plan section (markdown checkbox list) covering: `make arm` clean,
  `make test-applets` clean, all per-applet tests pass, all host routing
  tests pass, each pilot loads on hardware standalone, both hosts load
  and route correctly, both-hosts-loaded scenario tested (certified or
  documented constraint).

Do NOT delete bundled `Hemispheres.o` plus `Hemispheres2.o` in the pilot
release. Those deletions are cleanup-release scope.

When the pilot release PR is merged, the parent agent authors the
mass-port kickoff prompt at
`docs/superpowers/prompts/<DATE>-per-applet-mass-port-kickoff.md` and
stops. The mass-port release has its own session.
