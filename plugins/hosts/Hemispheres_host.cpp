// Hemispheres host plug-in.
//
// A 2-slot NT host that composes two Hemi per-applet plug-ins side-by-side.
// Slot 0 renders at origin (0, 0); slot 1 renders at origin (64, 0).
//
// Per-draw slot-resolution caching: resolve_slot() is called once per slot
// in draw().  The cached pointers are stored on the instance and used by
// customUi() to route encoder and button events without re-querying NT_getSlot
// on every UI event within the same draw cycle.
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

// ---------------------------------------------------------------------------
// Parameter table
// ---------------------------------------------------------------------------
//
//   v[0] = slot index watched for the left  (slot 0) region, default 0
//   v[1] = slot index watched for the right (slot 1) region, default 1

static constexpr int kNumParams = 2;

static const _NT_parameter s_params[kNumParams] = {
    { "Slot 0 index", 0, 15, 0, kNT_unitNone, kNT_scalingNone, nullptr },
    { "Slot 1 index", 0, 15, 1, kNT_unitNone, kNT_scalingNone, nullptr },
};

// ---------------------------------------------------------------------------
// Algorithm instance
// ---------------------------------------------------------------------------

struct _HHInstance : public _NT_algorithm {
    // Cached slot-resolution results from the most recent draw() call.
    // Populated by resolve_both_slots(); consumed by customUi().
    HemiPluginInterface* cached_slot[2];
};

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
extern "C" void hh_test_inject_slot(int slot_idx,
                                    HemiPluginInterface* plugin,
                                    uint32_t guid) {
    if (slot_idx < 0 || slot_idx >= 16) return;
    s_test_slots[slot_idx].valid  = true;
    s_test_slots[slot_idx].plugin = plugin;
    s_test_slots[slot_idx].guid   = guid;
    s_test_slots_active = true;
}

// Reset all injected slots (call between test cases).
extern "C" void hh_test_clear_slots(void) {
    for (int i = 0; i < 16; ++i) {
        s_test_slots[i] = { false, nullptr, 0 };
    }
    s_test_slots_active = false;
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

// Resolve both slots and cache the results on the instance.
static void resolve_and_cache(_HHInstance* inst) {
    for (int i = 0; i < 2; ++i) {
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
    req.sram  = sizeof(_HHInstance);
    req.dram  = 0;
    req.dtc   = 0;
    req.itc   = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& /*req*/,
                                     const int32_t* /*specifications*/) {
    auto* inst = new (ptrs.sram) _HHInstance();
    inst->parameters     = s_params;
    inst->parameterPages = nullptr;
    const_cast<int16_t*&>(inst->v) = nullptr;
    inst->cached_slot[0] = nullptr;
    inst->cached_slot[1] = nullptr;
    return inst;
}

static void step_impl(_NT_algorithm* /*self*/,
                      float* /*busFrames*/,
                      int /*numFramesBy4*/) {
    // Host owns no audio. Per-applet plug-ins loaded in their own NT slots
    // handle bus I/O when the firmware calls their step() directly.
}

static bool draw_impl(_NT_algorithm* self) {
    auto* inst = static_cast<_HHInstance*>(self);

    // Clear screen (firmware does not memset between draws; bundled host
    // does the same explicit clear at hemispheres_shim.h:208).
    std::memset(NT_screen, 0, 128 * 64);

    // Resolve both slots once for this draw cycle and cache the results.
    resolve_and_cache(inst);

    // Screen is 256 wide; each slot gets 128 px (matching bundled host layout).
    // Vendor applets draw at x = 0..63 relative to HS::gfx_offset; the 128-step
    // spacing leaves room for the applet's full draw plus any decoration.
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
