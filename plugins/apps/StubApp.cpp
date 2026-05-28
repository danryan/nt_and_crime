// StubApp: throwaway foundation app that proves the O_C-app build path end to
// end (host + ARM). It is NOT a real port; its only job is to exercise every
// seam the real apps (Low-rents, Harrington1200) will use, so it doubles as
// the canonical template for the per-app .cpp glue. The structure below is the
// pattern a real app fills in:
//
//   1. NT_OC_APP_TU + runtime include (aggregates the OC shim impl).
//   2. A settings::SettingsBase subclass + SETTINGS_DECLARE table.
//   3. The eleven OC::App thunks bound to a static App aggregate.
//   4. The _NT_algorithm subclass embedding the AppAlgorithm + settings.
//   5. The factory (correct vendor field order) + customUi emit glue.
//
// Replace the stub app logic with the vendor app's; keep the glue verbatim.

// Aggregation trigger: defining this BEFORE the runtime include pulls the OC
// shim impl into this single TU (see _per_app_runtime.h). The per-app host
// test TU does NOT define this, so only this .cpp aggregates and the shim
// globals are defined exactly once per linked binary.
#define NT_OC_APP_TU 1

#include "_per_app_runtime.h"

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"
#include "hem_graphics.h"          // graphics global for the menu draw
#include "util/util_settings.h"
#include "UI/ui_events.h"          // ::UI::Event for the customUi emit glue

#include "../../shim/include/oc_app_manifests/StubApp.h"

#include <distingnt/api.h>
#include <new>

// ---------------------------------------------------------------------------
// 2. Settings. A real app pulls its vendor settings class here. The stub
//    declares two settings so the round-trip path is exercised.
// ---------------------------------------------------------------------------

class StubSettings : public settings::SettingsBase<StubSettings, 2> {
public:
    enum {
        STUB_RATE = 0,
        STUB_MODE,
        STUB_LAST,
    };
};

namespace {
const char* const stub_mode_labels[] = { "off", "low", "high" };
}  // namespace

// SETTINGS_DECLARE expands to a `template<> constexpr ... value_attr_[]`
// specialization at namespace scope; it must live outside any function.
SETTINGS_DECLARE(StubSettings, 2) {
    {  0,  0, 16, "Rate", nullptr,           settings::STORAGE_TYPE_U8 },
    {  0,  0,  2, "Mode", stub_mode_labels,  settings::STORAGE_TYPE_U8 },
};

namespace {

using ManifestNS = oc_app::StubApp;

// ---------------------------------------------------------------------------
// 4. The algorithm instance. Embeds the runtime AppAlgorithm (which IS an
//    _NT_algorithm) plus the typed settings the facade adapts. A real app adds
//    its vendor app state here too. Settings live in the instance so they
//    persist across step/draw/serialise for the life of the algorithm.
// ---------------------------------------------------------------------------
struct StubInstance : public oc_runtime::AppAlgorithm {
    StubSettings settings;
};

// The single live instance pointer, set in construct(). The OC::App thunks are
// global (the vendor App model is a singleton dispatch table), so they reach
// the live instance through this pointer. One algorithm instance is live at a
// time per loaded slot; the firmware constructs/destructs around it.
StubInstance* g_instance = nullptr;

// Stub app state proving isr/loop/draw ran. A real app keeps its own state in
// StubInstance; this file-scope counter is sufficient for the foundation.
int g_isr_calls  = 0;
int g_loop_calls = 0;

// ---------------------------------------------------------------------------
// 3. The eleven OC::App thunks. A real app forwards each to the vendor app's
//    method of the same name. The stub implements minimal bodies.
// ---------------------------------------------------------------------------
void StubApp_init() {}
size_t StubApp_storageSize() { return StubSettings::storageSize(); }
size_t StubApp_save(void* blob) {
    return g_instance ? g_instance->settings.Save(blob) : 0;
}
size_t StubApp_restore(const void* blob) {
    return g_instance ? g_instance->settings.Restore(blob) : 0;
}
void StubApp_handleAppEvent(OC::AppEvent) {}
void StubApp_loop() { ++g_loop_calls; }
void StubApp_menu() {
    // Draw something so the host test can prove the menu reached NT_screen.
    // graphics writes into vendor x [0,128); the runtime centers it afterward.
    graphics.drawStr(8, 24, "STUB");
}
void StubApp_screensaver() {}
void StubApp_handleButtonEvent(const OC::UI::Event&) {}
void StubApp_handleEncoderEvent(const OC::UI::Event&) {}
void StubApp_isr() { ++g_isr_calls; }

// The App aggregate. Field order matches OC::App (OC_apps.h).
const OC::App the_stub_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              StubApp_init,
    /* storageSize */       StubApp_storageSize,
    /* Save */              StubApp_save,
    /* Restore */           StubApp_restore,
    /* HandleAppEvent */    StubApp_handleAppEvent,
    /* loop */              StubApp_loop,
    /* DrawMenu */          StubApp_menu,
    /* DrawScreensaver */   StubApp_screensaver,
    /* HandleButtonEvent */ StubApp_handleButtonEvent,
    /* HandleEncoderEvent */StubApp_handleEncoderEvent,
    /* isr */               StubApp_isr,
};

// ---------------------------------------------------------------------------
// 5a. Factory thunks. calculateRequirements/construct size and build the
//     instance; the rest forward to the runtime's factory thunks.
// ---------------------------------------------------------------------------
void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing
    // rows plus one row per settings entry. Over-sizing leaves phantom blank
    // parameters on the algo page (see CLAUDE.md numParameters gotcha).
    req.numParameters = oc_runtime::kIoParamCount + StubSettings::STUB_LAST;
    req.sram          = sizeof(StubInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) StubInstance();
    g_instance = inst;

    inst->settings.InitDefaults();
    g_isr_calls  = 0;
    g_loop_calls = 0;

    // Build the parameter table (I/O routing + one row per setting), wire the
    // settings facade, set OC::apps::current_app, fire APP_EVENT_RESUME, and
    // seed v[] from defaults. The templated construct() captures the typed
    // settings call sites.
    oc_runtime::construct(*inst, &the_stub_app, &inst->settings,
                          StubSettings::STUB_LAST);
    return inst;
}

// ---------------------------------------------------------------------------
// 5b. customUi emit glue. THE TEMPLATE FOR REAL APPS. The runtime owns the
//     control-edge bookkeeping (held_since timestamps, last_controls, idle
//     reset) but cannot construct a ::UI::Event because it deliberately does
//     not pull vendor UI/ui_events.h. So the per-app TU builds the event from
//     the runtime's classification + the mapping table and forwards it to the
//     vendor app, THEN calls oc_runtime::customUi to advance the bookkeeping.
//
//     OC::App::HandleButtonEvent takes a const OC::UI::Event& (forward-declared
//     in OC_apps.h); the concrete event is the top-level ::UI::Event from
//     ui_events.h. They are layout-identical, so reinterpret_cast bridges the
//     two distinct-but-compatible types the way the runtime's documented
//     composition pseudocode prescribes.
// ---------------------------------------------------------------------------
void emit_button(const StubInstance* inst, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void emit_encoder(const StubInstance* inst, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    auto* inst = static_cast<StubInstance*>(self);
    if (!inst->app) return;

    int n = 0;
    const oc_runtime::ControlMapping* tbl = oc_runtime::button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;

    // Buttons: emit on the release edge, classified short vs long by the
    // runtime. (LONG_PRESS-while-held emission is left to real apps that need
    // it; the foundation covers the short/long release path.)
    for (int i = 0; i < n; ++i) {
        const uint16_t bit  = tbl[i].nt_bit;
        const int      bi   = oc_runtime::bit_index(bit);
        const bool now_down = (data.controls & bit) != 0;
        if ((edges & bit) && !now_down) {
            const uint8_t ev = oc_runtime::classify_release(inst, bi);
            emit_button(inst, tbl[i].oc_control, ev);
        }
    }

    // Encoders: the runtime does not store deltas; read them straight from the
    // _NT_uiData and emit one EVENT_ENCODER per non-zero delta.
    if (data.encoders[0] != 0) {
        emit_encoder(inst, OC::CONTROL_ENCODER_L, data.encoders[0]);
    }
    if (data.encoders[1] != 0) {
        emit_encoder(inst, OC::CONTROL_ENCODER_R, data.encoders[1]);
    }

    // Advance the runtime bookkeeping AFTER emitting so held_since/last_controls
    // reflect the post-event state, matching the runtime's documented contract.
    oc_runtime::customUi(*inst, data);
}

// ---------------------------------------------------------------------------
// 5c. The factory. Field order follows _NT_factory (api.h:468): tags comes
//     BEFORE hasCustomUi/customUi, and serialise/deserialise come after. The
//     aeabi_probe factory is the reference for this order.
// ---------------------------------------------------------------------------
const _NT_factory factory = {
    .guid        = ManifestNS::guid,
    .name        = ManifestNS::name,
    .description = ManifestNS::description,
    .calculateRequirements = calculateRequirements_impl,
    .construct             = construct_impl,
    .parameterChanged      = oc_runtime::parameterChanged_factory,
    .step                  = oc_runtime::step_factory,
    .draw                  = oc_runtime::draw_factory,
    .tags                  = kNT_tagUtility,
    .hasCustomUi           = oc_runtime::hasCustomUi_factory,
    .customUi              = customUi_impl,
    .serialise             = oc_runtime::serialise_factory,
    .deserialise           = oc_runtime::deserialise_factory,
};

}  // namespace

extern "C" __attribute__((visibility("default")))
uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&factory : 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Test seams. Defined here because StubInstance/StubSettings are only fully
// visible in this TU. The host test reaches the embedded settings through
// these without pulling the SETTINGS_DECLARE specialization into its own TU.
// ---------------------------------------------------------------------------
int stub_app_get_setting(_NT_algorithm* self, int idx) {
    return static_cast<StubInstance*>(self)->settings.get_value(
        static_cast<size_t>(idx));
}
bool stub_app_apply_setting(_NT_algorithm* self, int idx, int value) {
    return static_cast<StubInstance*>(self)->settings.apply_value(
        static_cast<size_t>(idx), value);
}
int stub_app_setting_count() { return StubSettings::STUB_LAST; }
int stub_app_settings_param_base() { return oc_runtime::settings_param_base(); }
int stub_app_loop_calls(_NT_algorithm*) { return g_loop_calls; }
