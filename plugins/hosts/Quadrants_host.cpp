// Quadrants Host plug-in.
//
// Composes 4 HemiPluginInterface applets in a 2x2 grid on the 128x64 screen.
// Slot layout:
//   Slot 0: origin (0,  0) - top-left  (64x32)
//   Slot 1: origin (64, 0) - top-right (64x32)
//   Slot 2: origin (0, 32) - bot-left  (64x32)
//   Slot 3: origin (64,32) - bot-right (64x32)
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

// ---------------------------------------------------------------------------
// Parameter table
// ---------------------------------------------------------------------------
//
//   v[0] = slot index watched for slot 0 (top-left),  default 0
//   v[1] = slot index watched for slot 1 (top-right), default 1
//   v[2] = slot index watched for slot 2 (bot-left),  default 2
//   v[3] = slot index watched for slot 3 (bot-right), default 3

static constexpr int kNumParams = 4;

static const _NT_parameter s_params[kNumParams] = {
    { "Slot 0 index", 0, 15, 0, kNT_unitNone, kNT_scalingNone, nullptr },
    { "Slot 1 index", 0, 15, 1, kNT_unitNone, kNT_scalingNone, nullptr },
    { "Slot 2 index", 0, 15, 2, kNT_unitNone, kNT_scalingNone, nullptr },
    { "Slot 3 index", 0, 15, 3, kNT_unitNone, kNT_scalingNone, nullptr },
};

// Slot origin table (origin_x, origin_y).
static constexpr int kSlotOriginX[4] = {  0, 64,  0, 64 };
static constexpr int kSlotOriginY[4] = {  0,  0, 32, 32 };

// ---------------------------------------------------------------------------
// Algorithm instance
// ---------------------------------------------------------------------------

struct _QQInstance : public _NT_algorithm {
    uint8_t focused_slot_idx;  // 0-3; persisted via serialise/deserialise
    // Cached slot-resolution results from the most recent draw() call.
    // Populated by resolve_all_slots(); consumed by customUi().
    HemiPluginInterface* cached_slot[4];
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
static void resolve_and_cache(_QQInstance* inst) {
    for (int i = 0; i < 4; ++i) {
        uint32_t param_slot_idx = (inst->v != nullptr)
            ? (uint32_t)inst->v[i]
            : (uint32_t)i;
        host_helpers::ResolvedSlot r = get_resolved(param_slot_idx);
        inst->cached_slot[i] = r.plugin;
    }
}

// ---------------------------------------------------------------------------
// _NT_factory hooks
// ---------------------------------------------------------------------------

static void calculateRequirements_impl(_NT_algorithmRequirements& req,
                                       const int32_t* /*specifications*/) {
    req.numParameters = kNumParams;
    req.sram  = sizeof(_QQInstance);
    req.dram  = 0;
    req.dtc   = 0;
    req.itc   = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& /*req*/,
                                     const int32_t* /*specifications*/) {
    auto* inst = new (ptrs.sram) _QQInstance();
    inst->parameters     = s_params;
    inst->parameterPages = nullptr;
    const_cast<int16_t*&>(inst->v) = nullptr;
    inst->focused_slot_idx = 0;
    for (int i = 0; i < 4; ++i) inst->cached_slot[i] = nullptr;
    return inst;
}

static void parameterChanged_impl(_NT_algorithm* /*self*/, int /*p*/) {}

static void step_impl(_NT_algorithm* /*self*/,
                      float* /*busFrames*/,
                      int /*numFramesBy4*/) {
    // Host owns no audio. Per-applet plug-ins loaded in their own NT slots
    // handle bus I/O when the firmware calls their step() directly.
}

static bool draw_impl(_NT_algorithm* self) {
    auto* inst = static_cast<_QQInstance*>(self);

    // Resolve all 4 slots once for this draw cycle and cache the results.
    resolve_and_cache(inst);

    // Render each slot.
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

    // Draw focused-slot border: 1px inverted box around the focused region.
    {
        int fi = inst->focused_slot_idx & 3;
        int ox = kSlotOriginX[fi];
        int oy = kSlotOriginY[fi];
        NT_drawShapeI(kNT_box, ox, oy, ox + 63, oy + 31, 15);
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
