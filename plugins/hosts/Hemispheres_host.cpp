// Hemispheres host plug-in.
//
// A 2-slot NT host that composes two Hemi per-applet plug-ins side-by-side.
// Slot 0 renders at origin (0, 0); slot 1 renders at origin (128, 0).
//
// Per-draw slot-resolution caching: resolve_slot() is called once per slot
// in draw().  The cached pointers are stored on the instance and used by
// customUi() to route encoder and button events without re-querying NT_getSlot
// on every UI event within the same draw cycle.
//
// Parameter proxying (host-ux rework stage 3a):
//
//   The host's parameter table is owned by host_proxy::State on the instance.
//   Selectors at v[0..K-1] are enums that scroll Hemi-prefix algorithms in
//   the preset by name. Proxy parameters at v[K..] mirror each watched slot's
//   _NT_parameter[] entries; edits forward to the watched slot via
//   NT_setParameterFromUi. See docs/superpowers/specs/2026-05-21-host-ux-rework-design.md.
//
//   Construct-time guard: parameterChanged fires once per parameter during
//   construct, before the algorithm is registered. Forwarding via
//   NT_setParameterFromUi at that moment hard-crashes the device. Proxy
//   edits are suppressed until at least one draw() has run (draw_count > 0).
//   See CLAUDE.md "Construct-time parameterChanged hazard".
//
// Under NT_HEM_HOST_SIM (host test builds), a test-injection table lets tests
// pre-populate the cached slot pointers without going through NT_getSlot.

// Include <cstddef> before <distingnt/slot.h> to ensure NULL is in scope.
// slot.h references NULL but does not include <cstddef> itself. Because
// host_helpers.h includes slot.h after api.h (neither of which provides
// NULL on arm-none-eabi or clang without this pre-include), we must include
// <cstddef> here first so the include guard on slot.h prevents the problem
// include from re-firing when host_helpers.h is processed.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <distingnt/api.h>
#include <distingnt/slot.h>
#include <new>

#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/host_helpers.h"
#include "../../shim/include/host_proxy.h"

// ---------------------------------------------------------------------------
// Algorithm instance
// ---------------------------------------------------------------------------

struct _HHInstance : public _NT_algorithm {
    // Cached slot-resolution results from the most recent draw() call.
    // Populated by resolve_and_cache(); consumed by customUi().
    HemiPluginInterface* cached_slot[2];

    // Per-instance proxy aggregator state. Owns the dynamic parameter
    // table that inst->parameters points at, plus the enum string table
    // shared by both selectors.
    host_proxy::State state;
};

static constexpr int kHostSlots = 2;

// ---------------------------------------------------------------------------
// Test-injection support (host simulator only)
// ---------------------------------------------------------------------------

#ifdef NT_HEM_HOST_SIM
struct TestSlotEntry {
    bool                 valid;
    HemiPluginInterface* plugin;
    uint32_t             guid;
};

static TestSlotEntry s_test_slots[16];
static bool          s_test_slots_active = false;

// Called by tests to inject a stub HemiPluginInterface into a given
// slot-index position before calling draw() or customUi().
// Pass plugin=nullptr to simulate an empty or failed-validation slot.
//
// Also mirrors the injection into the host_proxy enum-scan table so the
// selector aggregator can resolve the slot by enum value. This preserves
// the pre-stage-3a contract that callers only need hh_test_inject_slot to
// make a slot visible to both draw routing AND the proxy selector.
extern "C" void hh_test_inject_slot(int slot_idx,
                                    HemiPluginInterface* plugin,
                                    uint32_t guid) {
    if (slot_idx < 0 || slot_idx >= 16) return;
    s_test_slots[slot_idx].valid  = true;
    s_test_slots[slot_idx].plugin = plugin;
    s_test_slots[slot_idx].guid   = guid;
    s_test_slots_active = true;
    // Mirror into host_proxy enum-scan table. Use a synthesized name so the
    // enum entry is present; params=nullptr means proxy aggregation produces
    // a 0-param lane (sufficient for the routing-only tests).
    char name[20];
    name[0] = 'S'; name[1] = 'l'; name[2] = 'o'; name[3] = 't';
    name[4] = '_';
    name[5] = static_cast<char>('0' + (slot_idx / 10));
    name[6] = static_cast<char>('0' + (slot_idx % 10));
    name[7] = '\0';
    host_proxy::hp_test_inject_slot(static_cast<uint32_t>(slot_idx),
                                    name, guid, 0, nullptr);
}

// Reset all injected slots (call between test cases). Also clears the
// host_proxy enum-scan table so cross-test state cannot leak.
extern "C" void hh_test_clear_slots(void) {
    for (int i = 0; i < 16; ++i) {
        s_test_slots[i] = { false, nullptr, 0 };
    }
    s_test_slots_active = false;
    host_proxy::hp_test_clear_slots();
}

// Test accessor: hand the instance's State* to tests so they can assert on
// enum_strs, maps, and draw_count without poking through the parameters
// pointer alone.
extern "C" host_proxy::State* hh_test_get_state(_NT_algorithm* alg) {
    return alg ? &static_cast<_HHInstance*>(alg)->state : nullptr;
}

static host_helpers::ResolvedSlot test_resolve(uint32_t slot_idx) {
    if (!s_test_slots_active) {
        return { nullptr, 0 };
    }
    if (slot_idx >= 16) return { nullptr, 0 };
    const TestSlotEntry& e = s_test_slots[slot_idx];
    if (!e.valid) return { nullptr, 0 };
    // Simulate the same validation that resolve_slot() performs:
    // if plugin is non-null it already has valid magic/version (caller's
    // responsibility for the happy-path stub); if null it represents a failed
    // validation or empty slot.
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

// Resolve both selector-driven slots and cache the results on the instance.
// Uses the proxy state's per-lane resolved slot_idx (set by aggregate_slot)
// when available so draw() and customUi() route to the user-selected slot
// rather than v[0]/v[1] taken raw.
static void resolve_and_cache(_HHInstance* inst) {
    for (int i = 0; i < kHostSlots; ++i) {
        uint32_t slot_idx = inst->state.maps[i].slot_idx;
        if (slot_idx == host_proxy::kInvalidSlotIdx) {
            // Fallback for the construct-time path (before any
            // parameterChanged on a selector): resolve to the default enum
            // value's preset slot.
            slot_idx = host_proxy::resolve_enum_to_slot(
                inst->state, inst->state.proxy_params[i].def);
        }
        if (slot_idx == host_proxy::kInvalidSlotIdx) {
            inst->cached_slot[i] = nullptr;
            continue;
        }
        host_helpers::ResolvedSlot r = get_resolved(slot_idx);
        inst->cached_slot[i] = r.plugin;
    }
}

// ---------------------------------------------------------------------------
// _NT_factory hooks
// ---------------------------------------------------------------------------

static void calculateRequirements_impl(_NT_algorithmRequirements& req,
                                       const int32_t* /*specifications*/) {
    // Hemispheres uses K = 2 lanes. Selectors live at proxy_params[0..1];
    // proxy params at proxy_params[K + lane*kMaxProxyParamsPerSlot ..].
    // Total used = K + K * kMaxProxyParamsPerSlot = 2 + 32 = 34.
    // host_proxy::State sizes proxy_params[] to the worst case (Quadrants,
    // 68); firmware must only see the valid 34 to avoid phantom params.
    req.numParameters = kHostSlots + kHostSlots * host_proxy::kMaxProxyParamsPerSlot;
    req.sram  = sizeof(_HHInstance);
    req.dram  = 0;
    req.dtc   = 0;
    req.itc   = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& /*req*/,
                                     const int32_t* /*specifications*/) {
    auto* inst = new (ptrs.sram) _HHInstance();
    inst->parameterPages = nullptr;
    const_cast<int16_t*&>(inst->v) = nullptr;
    inst->cached_slot[0] = nullptr;
    inst->cached_slot[1] = nullptr;

    // Stage proxy state: 2 selector lanes, blank enum table, blank proxy params.
    host_proxy::init(inst->state, kHostSlots);

    // Populate the enum table from the current preset scan before installing
    // the selectors. The spec footer mandates default enum values 1 and 2
    // (the first two Hemi-prefix algorithms in preset order). init_selector
    // clamps def against the live enum count, so we MUST refresh first or
    // init_selector would silently clamp def to 0 (only "---" exists pre-refresh).
    host_proxy::refresh_enum_strings(inst->state);

    // Install selectors at proxy_params[0] and proxy_params[1] with default
    // enum values 1 and 2.
    host_proxy::init_selector(inst->state, 0, "Slot 0", 1);
    host_proxy::init_selector(inst->state, 1, "Slot 1", 2);

    // Aggregate each lane based on the selector's default value. v[] is not
    // yet bound at construct on hardware; the helper looks up the slot index
    // via the parameter's `def` field directly.
    for (int lane = 0; lane < kHostSlots; ++lane) {
        uint32_t slot_idx = host_proxy::resolve_enum_to_slot(
            inst->state, inst->state.proxy_params[lane].def);
        host_proxy::aggregate_slot(inst->state, lane, slot_idx);
    }

    inst->parameters = inst->state.proxy_params;
    return inst;
}

static void step_impl(_NT_algorithm* /*self*/,
                      float* /*busFrames*/,
                      int /*numFramesBy4*/) {
    // Host owns no audio. Per-applet plug-ins loaded in their own NT slots
    // handle bus I/O when the firmware calls their step() directly.
}

static void parameterChanged_impl(_NT_algorithm* self, int p) {
    auto* inst = static_cast<_HHInstance*>(self);
    auto& s    = inst->state;

    // Selector branch: refresh enum strings and reaggregate this lane.
    if (p < s.kNumSlotIndexParams) {
        int lane = p;
        host_proxy::refresh_enum_strings(s);
        int32_t alg_idx = NT_algorithmIndex(self);
        if (alg_idx >= 0) {
            NT_updateParameterDefinition(static_cast<uint32_t>(alg_idx),
                                         static_cast<uint32_t>(p));
        }
        int16_t enum_value = (inst->v != nullptr) ? inst->v[p]
                                                  : s.proxy_params[p].def;
        uint32_t new_slot = host_proxy::resolve_enum_to_slot(s, enum_value);
        host_proxy::aggregate_slot(s, lane, new_slot);
        if (alg_idx >= 0) {
            int base = s.kNumSlotIndexParams + lane * host_proxy::kMaxProxyParamsPerSlot;
            for (int pp = 0; pp < host_proxy::kMaxProxyParamsPerSlot; ++pp) {
                NT_updateParameterDefinition(static_cast<uint32_t>(alg_idx),
                                             static_cast<uint32_t>(base + pp));
            }
        }
        return;
    }

    // Proxy branch. Construct-time guard: firmware fires parameterChanged
    // for each param during construct before the algorithm is registered.
    // Forwarding via NT_setParameterFromUi at that moment hard-crashes the
    // device. Only forward once at least one draw() has run.
    if (s.draw_count == 0) return;
    if (inst->v == nullptr) return;

    host_proxy::ForwardTarget t = host_proxy::decode_forward(s, p);
    if (t.slot_idx == host_proxy::kInvalidSlotIdx) return;
    NT_setParameterFromUi(t.slot_idx,
                          static_cast<uint32_t>(t.slot_param_idx),
                          inst->v[p]);
    // No re-entry guard required: probe confirmed NEST == 0 (firmware defers
    // the downstream parameterChanged to a later frame). See spec
    // docs/superpowers/specs/2026-05-21-host-ux-rework-design.md.
}

static bool draw_impl(_NT_algorithm* self) {
    auto* inst = static_cast<_HHInstance*>(self);

    // Flag the algorithm as alive so the proxy branch of parameterChanged
    // is allowed to forward. Must increment before any work that could
    // recursively invoke parameterChanged.
    ++inst->state.draw_count;

    // Opportunistic enum refresh: catch preset edits that happen between
    // draws. If the set changed, reissue NT_updateParameterDefinition for
    // each selector and reaggregate every lane (stale slot_idx mappings
    // would forward to the wrong watched slot otherwise).
    if (host_proxy::refresh_enum_strings(inst->state)) {
        int32_t alg_idx = NT_algorithmIndex(self);
        if (alg_idx >= 0) {
            for (int lane = 0; lane < inst->state.kNumSlotIndexParams; ++lane) {
                NT_updateParameterDefinition(static_cast<uint32_t>(alg_idx),
                                             static_cast<uint32_t>(lane));
            }
        }
        for (int lane = 0; lane < inst->state.kNumSlotIndexParams; ++lane) {
            int16_t enum_value = (inst->v != nullptr) ? inst->v[lane]
                                                      : inst->state.proxy_params[lane].def;
            uint32_t new_slot = host_proxy::resolve_enum_to_slot(inst->state, enum_value);
            host_proxy::aggregate_slot(inst->state, lane, new_slot);
        }
    }

    // Clear screen (firmware does not memset between draws; bundled host
    // does the same explicit clear at hemispheres_shim.h:208).
    std::memset(NT_screen, 0, 128 * 64);

    // Resolve both slots once for this draw cycle and cache the results.
    resolve_and_cache(inst);

    // Screen is 256 wide; each slot gets 128 px (matching bundled host layout).
    // Vendor applets draw at x = 0..63 relative to HS::gfx_offset; the 128-step
    // spacing leaves room for the applet's full draw plus any decoration.
    // Per-applet clip rect (Q1) is set inside render_view_with_offset in each
    // per-applet plug-in's TU; the host does not touch HS::gfx_clip_w/h.
    static constexpr int origins[2] = { 0, 128 };
    for (int i = 0; i < 2; ++i) {
        HemiPluginInterface* p = inst->cached_slot[i];
        if (p && p->render_view) {
            p->render_view(p, origins[i], 0);
        } else {
            host_helpers::render_incompatible_stub(origins[i], 0);
        }
    }

    // Return true to suppress the default parameter strip.
    return true;
}

static uint32_t hasCustomUi_impl(_NT_algorithm* /*self*/) {
    return kNT_encoderL | kNT_encoderR
         | kNT_encoderButtonL | kNT_encoderButtonR
         | kNT_button1 | kNT_button2;
}

static void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    auto* inst = static_cast<_HHInstance*>(self);

    // Use cached slot pointers populated during the most recent draw().
    // If draw() has not yet run, resolve now so customUi() is safe on first
    // call before the first draw (e.g., during a test that skips draw).
    if (inst->cached_slot[0] == nullptr && inst->cached_slot[1] == nullptr) {
        resolve_and_cache(inst);
    }

    HemiPluginInterface* s0 = inst->cached_slot[0];
    HemiPluginInterface* s1 = inst->cached_slot[1];

    // L encoder turn -> slot 0 on_encoder_turn
    if (data.encoders[0] != 0 && s0 && s0->on_encoder_turn) {
        s0->on_encoder_turn(s0, data.encoders[0]);
    }

    // R encoder turn -> slot 1 on_encoder_turn
    if (data.encoders[1] != 0 && s1 && s1->on_encoder_turn) {
        s1->on_encoder_turn(s1, data.encoders[1]);
    }

    // L encoder button edge -> slot 0 on_button_press
    if ((data.controls & kNT_encoderButtonL) &&
        !(data.lastButtons & kNT_encoderButtonL)) {
        if (s0 && s0->on_button_press) s0->on_button_press(s0);
    }

    // R encoder button edge -> slot 1 on_button_press
    if ((data.controls & kNT_encoderButtonR) &&
        !(data.lastButtons & kNT_encoderButtonR)) {
        if (s1 && s1->on_button_press) s1->on_button_press(s1);
    }

    // button1 edge -> slot 0 on_aux_button
    if ((data.controls & kNT_button1) &&
        !(data.lastButtons & kNT_button1)) {
        if (s0 && s0->on_aux_button) s0->on_aux_button(s0);
    }

    // button2 edge -> slot 1 on_aux_button
    if ((data.controls & kNT_button2) &&
        !(data.lastButtons & kNT_button2)) {
        if (s1 && s1->on_aux_button) s1->on_aux_button(s1);
    }
}

// ---------------------------------------------------------------------------
// Factory and plug-in entry point
// ---------------------------------------------------------------------------

static const _NT_factory factory = {
    .guid        = NT_MULTICHAR('H','m','H','h'),
    .name        = "Hemispheres Host",
    .description = "Composes 2 Hemi applets in slots",
    .calculateRequirements = calculateRequirements_impl,
    .construct             = construct_impl,
    .parameterChanged      = parameterChanged_impl,
    .step                  = step_impl,
    .draw                  = draw_impl,
    .hasCustomUi           = hasCustomUi_impl,
    .customUi              = customUi_impl,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&factory : 0;
        default: break;
    }
    return 0;
}
