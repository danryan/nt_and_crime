// Quadrants Host plug-in.
//
// Composes 4 HemiPluginInterface applets in a 1x4 row on the 256x64 screen.
// Slot layout:
//   Slot 0: origin (  0, 0) - 64-px column 0
//   Slot 1: origin ( 64, 0) - 64-px column 1
//   Slot 2: origin (128, 0) - 64-px column 2
//   Slot 3: origin (192, 0) - 64-px column 3
// All slots share the full 64-row vertical range. HS::gfx_offset_y stays
// 0 for this layout; the shim Y-offset machinery lives in the runtime
// helper for future grid topologies.
//
// Focused-slot control (spec: 2026-05-19-per-applet-pilot-design.md):
//   button1-4 edges set focused slot index (0-3).
//   L encoder turn  -> focused slot on_encoder_turn(direction)
//   R encoder turn  -> focused slot on_encoder_turn_shifted(direction)
//   L encoder button edge -> focused slot on_button_press
//   R encoder button edge -> focused slot on_aux_button
//
// Parameters: v[0..3] are the 4 slot indices the host watches.
// Serialise/deserialise: persists focused_slot_idx.
//
// Note on host test build (NULL compatibility):
// Including <cstddef> and <distingnt/slot.h> before <distingnt/host_helpers.h>
// ensures that when host_helpers.h transitively includes slot.h the include
// guard fires and slot.h is not re-processed. This gives NULL to slot.h within
// this TU. The independent TU shim/src/host_helpers.cpp does not benefit from
// this ordering; the integration step must add #include <cstddef> to
// shim/include/host_helpers.h to fix that TU on macOS clang++.

// Include <cstddef> before slot.h to ensure NULL is defined; then use the
// include guard to prevent slot.h from being re-processed without NULL when
// host_helpers.h is later included.
#include <cstddef>
#include <cstdint>
#include <new>
#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <distingnt/slot.h>

#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/host_helpers.h"
#include "../../shim/include/host_proxy.h"

// ---------------------------------------------------------------------------
// Parameter table
// ---------------------------------------------------------------------------
//
// The host owns a host_proxy::State that builds the parameter table at
// construct time:
//
//   v[0..3]  = Slot-N enum selectors. Each enum maps preset-order Hemi-
//              prefix algorithms to a watched lane (0 = "---" / unbound).
//   v[4..67] = Proxy params for each watched lane. Lane L occupies
//              [4 + L*16 .. 4 + L*16 + 16). Names prefixed "S0 ".."S3 ".
//
// Editing a selector re-aggregates that lane and pushes updated parameter
// definitions back to the firmware via NT_updateParameterDefinition.
// Editing a proxy parameter forwards the value to the watched slot via
// NT_setParameterFromUi after the first draw() (construct-time guard
// suppresses the firmware's fan-out parameterChanged calls).

// Slot origin table (origin_x, origin_y). 1x4 layout on 256x64 screen.
static constexpr int kSlotOriginX[4] = {  0, 64, 128, 192 };
static constexpr int kSlotOriginY[4] = {  0,  0,   0,   0 };

// ---------------------------------------------------------------------------
// Algorithm instance
// ---------------------------------------------------------------------------

struct _QQInstance : public _NT_algorithm {
    uint8_t focused_slot_idx;  // 0-3; persisted via serialise/deserialise
    // Cached slot-resolution results from the most recent draw() call.
    // Populated by resolve_all_slots(); consumed by customUi().
    HemiPluginInterface* cached_slot[4];
    // Per-host proxy aggregator. Owns selector + proxy parameter table
    // (inst->parameters = state.proxy_params).
    host_proxy::State state;
};

// ---------------------------------------------------------------------------
// Test-injection support (host simulator only)
// ---------------------------------------------------------------------------

#ifdef NT_HEM_HOST_SIM
struct QQTestSlotEntry {
    bool                 valid;
    HemiPluginInterface* plugin;
    uint32_t             guid;
};

static QQTestSlotEntry s_test_slots[16];
static bool            s_test_slots_active = false;

// Called by tests to inject a stub HemiPluginInterface into a given
// slot-index position before calling draw() or customUi().
// Pass plugin=nullptr to simulate an empty or failed-validation slot.
extern "C" void qq_test_inject_slot(int slot_idx,
                                    HemiPluginInterface* plugin,
                                    uint32_t guid) {
    if (slot_idx < 0 || slot_idx >= 16) return;
    s_test_slots[slot_idx].valid  = true;
    s_test_slots[slot_idx].plugin = plugin;
    s_test_slots[slot_idx].guid   = guid;
    s_test_slots_active = true;
}

// Reset all injected slots (call between test cases).
extern "C" void qq_test_clear_slots(void) {
    for (int i = 0; i < 16; ++i) {
        s_test_slots[i] = { false, nullptr, 0 };
    }
    s_test_slots_active = false;
}

// Expose focused_slot_idx for assertions.
extern "C" uint8_t qq_test_get_focused_slot(const _NT_algorithm* self) {
    return static_cast<const _QQInstance*>(self)->focused_slot_idx;
}

// Override focused_slot_idx for test setup.
extern "C" void qq_test_set_focused_slot(_NT_algorithm* self, uint8_t idx) {
    static_cast<_QQInstance*>(self)->focused_slot_idx = idx & 3;
}

static host_helpers::ResolvedSlot test_resolve(uint32_t slot_idx) {
    if (!s_test_slots_active) {
        return { nullptr, 0 };
    }
    if (slot_idx >= 16) return { nullptr, 0 };
    const QQTestSlotEntry& e = s_test_slots[slot_idx];
    if (!e.valid) return { nullptr, 0 };
    // Mirror host_helpers::resolve_slot validation so QH6 (ABI mismatch)
    // and wrong-guid tests exercise the same path as production: reject
    // any slot whose guid prefix is not 'Hm' or whose magic/version
    // fields fail validation.
    if ((e.guid & 0xFFFF) != kHemiGuidPrefix) {
        return { nullptr, e.guid };
    }
    if (!e.plugin) {
        return { nullptr, e.guid };
    }
    if (e.plugin->magic != kHemiInterfaceMagic) {
        return { nullptr, e.guid };
    }
    if (e.plugin->interface_version < kHemiInterfaceVersion) {
        return { nullptr, e.guid };
    }
    return { e.plugin, e.guid };
}
#endif  // NT_HEM_HOST_SIM

// ---------------------------------------------------------------------------
// Slot resolution helper
// ---------------------------------------------------------------------------

static host_helpers::ResolvedSlot get_resolved(uint32_t slot_idx) {
#ifdef NT_HEM_HOST_SIM
    if (s_test_slots_active) {
        return test_resolve(slot_idx);
    }
#endif
    return host_helpers::resolve_slot(slot_idx);
}

// Resolve all 4 slots and cache the results on the instance.
//
// v[0..3] are enum selector values from host_proxy::State::enum_strs;
// resolve each to its preset slot index before calling host_helpers.
// During tests run before draw_count > 0 (or before v is bound on
// hardware), fall back to the selector's def value.
static void resolve_and_cache(_QQInstance* inst) {
    for (int i = 0; i < 4; ++i) {
        int enum_value = (inst->v != nullptr)
            ? (int)inst->v[i]
            : (int)inst->state.proxy_params[i].def;
        uint32_t param_slot_idx = host_proxy::resolve_enum_to_slot(inst->state, enum_value);
        if (param_slot_idx == host_proxy::kInvalidSlotIdx) {
            inst->cached_slot[i] = nullptr;
            continue;
        }
        host_helpers::ResolvedSlot r = get_resolved(param_slot_idx);
        inst->cached_slot[i] = r.plugin;
    }
}

// ---------------------------------------------------------------------------
// _NT_factory hooks
// ---------------------------------------------------------------------------

static constexpr int kNumLanes = 4;

static void calculateRequirements_impl(_NT_algorithmRequirements& req,
                                       const int32_t* /*specifications*/) {
    // K selectors at [0..K-1] plus K * kMaxProxyParamsPerSlot proxy params
    // at [K..]. For Quadrants K = 4, total = 68 (== host_proxy::kMaxHostParams).
    req.numParameters = host_proxy::kMaxHostParams;
    req.sram  = sizeof(_QQInstance);
    req.dram  = 0;
    req.dtc   = 0;
    req.itc   = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& /*req*/,
                                     const int32_t* /*specifications*/) {
    auto* inst = new (ptrs.sram) _QQInstance();
    inst->parameterPages = nullptr;
    const_cast<int16_t*&>(inst->v) = nullptr;
    inst->focused_slot_idx = 0;
    for (int i = 0; i < 4; ++i) inst->cached_slot[i] = nullptr;

    // Initialize the proxy aggregator: 4 selector lanes. Scan the preset
    // for Hemi-prefix algorithms first so the enum table is populated
    // before init_selector clamps each default against enum_strs.count.
    // Then install one selector per lane with default value (lane + 1)
    // pointing to the Nth Hemi-prefix entry in preset-scan order.
    host_proxy::State& s = inst->state;
    host_proxy::init(s, kNumLanes);
    host_proxy::refresh_enum_strings(s);
    for (int lane = 0; lane < kNumLanes; ++lane) {
        char name[8] = { 'S','l','o','t',' ',
                         static_cast<char>('0' + lane), '\0', '\0' };
        host_proxy::init_selector(s, lane, name, lane + 1);
    }
    for (int lane = 0; lane < kNumLanes; ++lane) {
        // v[] is not yet bound at construct on hardware; aggregate against
        // each selector's `def` as the canonical initial enum value.
        uint32_t resolved = host_proxy::resolve_enum_to_slot(s, s.proxy_params[lane].def);
        host_proxy::aggregate_slot(s, lane, resolved);
    }
    inst->parameters = s.proxy_params;
    return inst;
}

static void parameterChanged_impl(_NT_algorithm* self, int host_p) {
    auto* inst = static_cast<_QQInstance*>(self);
    host_proxy::State& s = inst->state;

    if (host_p < s.kNumSlotIndexParams) {
        int lane = host_p;
        host_proxy::refresh_enum_strings(s);
        int32_t alg_idx = NT_algorithmIndex(self);
        if (alg_idx >= 0) {
            NT_updateParameterDefinition((uint32_t)alg_idx, (uint32_t)host_p);
        }
        int enum_value = inst->v != nullptr ? inst->v[host_p] : s.proxy_params[host_p].def;
        uint32_t new_slot = host_proxy::resolve_enum_to_slot(s, enum_value);
        host_proxy::aggregate_slot(s, lane, new_slot);
        int base = s.kNumSlotIndexParams + lane * host_proxy::kMaxProxyParamsPerSlot;
        if (alg_idx >= 0) {
            for (int p = 0; p < host_proxy::kMaxProxyParamsPerSlot; ++p) {
                NT_updateParameterDefinition((uint32_t)alg_idx, (uint32_t)(base + p));
            }
        }
        return;
    }

    // Construct-time guard: firmware fires parameterChanged for every
    // parameter during construct, before the algorithm is fully registered.
    // Forwarding via NT_setParameterFromUi at that moment hard-crashes the
    // device (see CLAUDE.md "Construct-time parameterChanged hazard" and
    // the reentrancy_probe report). Only forward once draw() has run at
    // least once. NEST == 0 was measured on hardware, so no additional
    // re-entry guard is required.
    if (s.draw_count == 0) return;
    host_proxy::ForwardTarget t = host_proxy::decode_forward(s, host_p);
    if (t.slot_idx == host_proxy::kInvalidSlotIdx) return;
    int16_t value = inst->v != nullptr ? inst->v[host_p] : 0;
    NT_setParameterFromUi(t.slot_idx, (uint32_t)t.slot_param_idx, value);
}

static void step_impl(_NT_algorithm* /*self*/,
                      float* /*busFrames*/,
                      int /*numFramesBy4*/) {
    // Host owns no audio. Per-applet plug-ins loaded in their own NT slots
    // handle bus I/O when the firmware calls their step() directly.
}

static bool draw_impl(_NT_algorithm* self) {
    auto* inst = static_cast<_QQInstance*>(self);
    // Increment first: the construct-time guard in parameterChanged
    // looks for draw_count > 0 as the signal that the algorithm has
    // completed its registration and synchronous forwarding is safe.
    ++inst->state.draw_count;

    // Opportunistic enum rescan once per draw to catch preset edits.
    // If the enum set changed, push fresh definitions for each selector
    // and re-aggregate every lane (its enum-to-slot resolution may have
    // moved, even if its enum value did not).
    if (host_proxy::refresh_enum_strings(inst->state)) {
        int32_t alg_idx = NT_algorithmIndex(self);
        for (int lane = 0; lane < kNumLanes; ++lane) {
            int enum_value = inst->v != nullptr
                ? inst->v[lane]
                : inst->state.proxy_params[lane].def;
            uint32_t resolved = host_proxy::resolve_enum_to_slot(inst->state, enum_value);
            host_proxy::aggregate_slot(inst->state, lane, resolved);
            if (alg_idx >= 0) {
                NT_updateParameterDefinition((uint32_t)alg_idx, (uint32_t)lane);
                int base = inst->state.kNumSlotIndexParams +
                           lane * host_proxy::kMaxProxyParamsPerSlot;
                for (int p = 0; p < host_proxy::kMaxProxyParamsPerSlot; ++p) {
                    NT_updateParameterDefinition((uint32_t)alg_idx, (uint32_t)(base + p));
                }
            }
        }
    }

    // Resolve all 4 slots once for this draw cycle and cache the results.
    resolve_and_cache(inst);

    // Render each slot. Per-applet clip rect (Q1) is set inside
    // render_view_with_offset in each per-applet plug-in's TU, bounding
    // emissions to a 64x64 rect around the slot origin. The host does
    // not touch HS::gfx_clip_w/h; the focused-slot border below uses
    // NT_drawShapeI directly and is unaffected by the per-applet clip.
    for (int i = 0; i < 4; ++i) {
        int ox = kSlotOriginX[i];
        int oy = kSlotOriginY[i];
        HemiPluginInterface* p = inst->cached_slot[i];
        if (p && p->render_view) {
            p->render_view(p, ox, oy);
        } else {
            host_helpers::render_incompatible_stub(ox, oy);
        }
    }

    // Draw focused-slot border: 1px inverted box around the focused region
    // (64-wide column, full 64-row height).
    {
        int fi = inst->focused_slot_idx & 3;
        int ox = kSlotOriginX[fi];
        int oy = kSlotOriginY[fi];
        NT_drawShapeI(kNT_box, ox, oy, ox + 63, oy + 63, 15);
    }

    return true;
}

static uint32_t hasCustomUi_impl(_NT_algorithm* /*self*/) {
    return kNT_encoderL | kNT_encoderR
         | kNT_encoderButtonL | kNT_encoderButtonR
         | kNT_button1 | kNT_button2 | kNT_button3 | kNT_button4;
}

static void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    auto* inst = static_cast<_QQInstance*>(self);

    // Button edges set focused slot.
    if ((data.controls & kNT_button1) && !(data.lastButtons & kNT_button1)) {
        inst->focused_slot_idx = 0;
    }
    if ((data.controls & kNT_button2) && !(data.lastButtons & kNT_button2)) {
        inst->focused_slot_idx = 1;
    }
    if ((data.controls & kNT_button3) && !(data.lastButtons & kNT_button3)) {
        inst->focused_slot_idx = 2;
    }
    if ((data.controls & kNT_button4) && !(data.lastButtons & kNT_button4)) {
        inst->focused_slot_idx = 3;
    }

    // Ensure cached slots are populated (first customUi before draw is safe).
    bool cache_empty = true;
    for (int i = 0; i < 4; ++i) {
        if (inst->cached_slot[i] != nullptr) { cache_empty = false; break; }
    }
    // Also check if test slots are active (populated but plugin may be null).
    (void)cache_empty;
#ifdef NT_HEM_HOST_SIM
    if (cache_empty && !s_test_slots_active) {
        resolve_and_cache(inst);
    } else if (s_test_slots_active) {
        resolve_and_cache(inst);
    }
#else
    if (cache_empty) {
        resolve_and_cache(inst);
    }
#endif

    // Route controls to the focused slot.
    int fi = inst->focused_slot_idx & 3;
    HemiPluginInterface* p = inst->cached_slot[fi];

    if (p == nullptr) {
        return;
    }

    // L encoder -> on_encoder_turn
    if (data.encoders[0] != 0 && p->on_encoder_turn) {
        p->on_encoder_turn(p, data.encoders[0]);
    }

    // R encoder -> on_encoder_turn_shifted
    if (data.encoders[1] != 0 && p->on_encoder_turn_shifted) {
        p->on_encoder_turn_shifted(p, data.encoders[1]);
    }

    // L encoder button edge -> on_button_press
    if ((data.controls & kNT_encoderButtonL) &&
        !(data.lastButtons & kNT_encoderButtonL)) {
        if (p->on_button_press) p->on_button_press(p);
    }

    // R encoder button edge -> on_aux_button
    if ((data.controls & kNT_encoderButtonR) &&
        !(data.lastButtons & kNT_encoderButtonR)) {
        if (p->on_aux_button) p->on_aux_button(p);
    }
}

static void serialise_impl(_NT_algorithm* self, _NT_jsonStream& stream) {
    auto* inst = static_cast<_QQInstance*>(self);
    stream.addMemberName("focused_slot");
    stream.addNumber((int)inst->focused_slot_idx);
}

static bool deserialise_impl(_NT_algorithm* self, _NT_jsonParse& parse) {
    auto* inst = static_cast<_QQInstance*>(self);
    int num_members = 0;
    if (!parse.numberOfObjectMembers(num_members)) return false;
    for (int i = 0; i < num_members; ++i) {
        if (parse.matchName("focused_slot")) {
            int v = 0;
            if (!parse.number(v)) return false;
            if (v >= 0 && v <= 3) {
                inst->focused_slot_idx = (uint8_t)v;
            }
        } else {
            if (!parse.skipMember()) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Factory and plug-in entry point
// ---------------------------------------------------------------------------

static const _NT_factory s_factory = {
    .guid        = NT_MULTICHAR('H','m','Q','q'),
    .name        = "Quadrants Host",
    .description = "Composes 4 Hemi applets with focused-slot control",
    .calculateRequirements = calculateRequirements_impl,
    .construct             = construct_impl,
    .parameterChanged      = parameterChanged_impl,
    .step                  = step_impl,
    .draw                  = draw_impl,
    .hasCustomUi           = hasCustomUi_impl,
    .customUi              = customUi_impl,
    .serialise             = serialise_impl,
    .deserialise           = deserialise_impl,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&s_factory : 0;
        default: break;
    }
    return 0;
}
