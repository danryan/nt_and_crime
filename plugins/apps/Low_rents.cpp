// Low-rents: O_C APP_LORENZ port (Lorenz and Rossler chaotic generators).
//
// One NT plug-in compiling to one small .o. It embeds exactly one vendor
// OC::App thunk table built from the LORENZ_* thunks the vendor header defines
// with internal (static) linkage, points OC::apps::current_app at it, and
// drives it from the NT plug-in entry points through the shared per-app
// runtime. The vendor app body (LORENZ_isr, LORENZ_menu, the event handlers,
// the LorenzGenerator settings class) compiles unmodified.
//
// Structure follows plugins/apps/StubApp.cpp (the canonical template). The one
// structural difference: the vendor app owns a file-scope singleton
// `lorenz_generator` (its SettingsBase instance), so the settings facade points
// at that vendor singleton rather than an instance-embedded settings object.

// Aggregation trigger: defining this BEFORE the runtime include pulls the OC
// shim impl into this single TU (see _per_app_runtime.h). The per-app host test
// TU does NOT define this, so only this .cpp aggregates and the shim globals
// are defined exactly once per linked binary.
#define NT_OC_APP_TU 1

#include "_per_app_runtime.h"
#include "oc_customui_dispatch.h"

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "OC_config.h"
#include "OC_menus.h"
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "util/util_math.h"
#include "UI/ui_events.h"          // ::UI::Event for the customUi emit glue

#include "../../shim/include/oc_app_manifests/Low_rents.h"

#include <distingnt/api.h>
#include <new>

// The vendor app body reaches the hand-ported menu widgets as bare `menu::`.
// The shim lays them out in OC::menu (OC_menus.h). A full `using namespace OC`
// would also drag OC::UI (forward-declared in OC_apps.h) into the global scope
// and make the vendor app's `UI::Event` ambiguous against the real ::UI from
// ui_events.h. A namespace alias is surgical: it binds bare `menu::` to OC::menu
// without touching any other name. Everything else the vendor app uses is either
// OC-qualified (OC::ADC, OC::DAC, OC::vectorscope_render, OC::CONTROL_*) or
// global (SmoothedValue, SCALE8_16, USAT16, graphics, ::UI::Event).
namespace menu = OC::menu;

// Enable the vendor app body and pull it in. APP_LORENZ.h is guarded by
// ENABLE_APP_LORENZ; defining it before the include compiles the LorenzGenerator
// settings class, the file-scope `lorenz_generator` singleton, and the eleven
// LORENZ_* thunks into this TU. The thunks have internal linkage, so the OC::App
// aggregate below must be built in this same TU.
#define ENABLE_APP_LORENZ 1
#include "APP_LORENZ.h"

namespace {

using ManifestNS = oc_app::Low_rents;

// The runtime AppAlgorithm instance. The vendor app keeps all of its state
// (the LorenzGenerator singleton, the menu cursor) at file scope, so the
// instance only carries the runtime base. The settings facade points at the
// vendor `lorenz_generator` singleton.
struct LorenzInstance : public oc_runtime::AppAlgorithm {};

// The single live instance pointer, set in construct(). The OC::App thunks are
// the vendor file-scope statics; they reach the vendor singleton directly, so
// this pointer only services the test seams and the customUi push-back.
LorenzInstance* g_instance = nullptr;

// OC::App declares HandleButtonEvent / HandleEncoderEvent as
// void(*)(const OC::UI::Event&) because OC_apps.h only forward-declares
// OC::UI::Event. The vendor thunks take the real top-level ::UI::Event (from
// ui_events.h). The two event types are layout-identical (the foundation's
// documented bridge assumption; StubApp reinterpret_casts the event reference
// across the same boundary), so the event-handler function pointers are bridged
// with a reinterpret_cast here. This is the symmetric counterpart of the
// reinterpret_cast in emit_button / emit_encoder below.
using OcEventFn = void (*)(const OC::UI::Event&);

// The App aggregate. Field order matches OC::App (OC_apps.h) and the vendor
// DECLARE_APP expansion (OC_apps.cpp:83): id, name, then the eleven thunks.
const OC::App the_lorenz_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              LORENZ_init,
    /* storageSize */       LORENZ_storageSize,
    /* Save */              LORENZ_save,
    /* Restore */           LORENZ_restore,
    /* HandleAppEvent */    LORENZ_handleAppEvent,
    /* loop */              LORENZ_loop,
    /* DrawMenu */          LORENZ_menu,
    /* DrawScreensaver */   LORENZ_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(LORENZ_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(LORENZ_handleEncoderEvent),
    /* isr */               LORENZ_isr,
};

// ---------------------------------------------------------------------------
// Settings facade bound to the vendor `lorenz_generator` singleton. The
// per-app runtime's templated construct() wires the facade from a
// SettingsBase* and a setting count; lorenz_generator IS a
// SettingsBase<LorenzGenerator, LORENZ_SETTING_LAST>.
// ---------------------------------------------------------------------------

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing
    // rows plus one row per settings entry (CLAUDE.md numParameters gotcha).
    req.numParameters = oc_runtime::kIoParamCount + LORENZ_SETTING_LAST;
    req.sram          = sizeof(LorenzInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) LorenzInstance();
    g_instance = inst;

    // LORENZ_init() (fired inside construct via app->Init) calls
    // lorenz_generator.Init(), which runs InitDefaults() and seeds the
    // generators. Wire the facade to the vendor singleton and build the
    // parameter table (I/O routing + one row per setting), set current_app,
    // and seed v[] from the post-default settings.
    oc_runtime::construct(*inst, &the_lorenz_app, &lorenz_generator,
                          LORENZ_SETTING_LAST);
    return inst;
}

// ---------------------------------------------------------------------------
// The factory. Field order follows _NT_factory (api.h:468): tags BEFORE
// hasCustomUi/customUi, serialise/deserialise after. See aeabi_probe.cpp.
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
    .customUi              = oc_runtime::dispatch_custom_ui_factory<false>,
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
// Test seams. Defined here because the vendor LorenzGenerator type and the
// `lorenz_generator` singleton are only fully visible in this TU. The host test
// reaches the embedded settings and the runtime view through these without
// pulling the vendor header (and its SETTINGS_DECLARE specialization) into its
// own TU.
// ---------------------------------------------------------------------------
int low_rents_get_setting(_NT_algorithm* /*self*/, int idx) {
    return lorenz_generator.get_value(static_cast<size_t>(idx));
}
bool low_rents_apply_setting(_NT_algorithm* /*self*/, int idx, int value) {
    return lorenz_generator.apply_value(static_cast<size_t>(idx), value);
}
int low_rents_setting_count() { return LORENZ_SETTING_LAST; }
int low_rents_settings_param_base() { return oc_runtime::settings_param_base(); }

// Drive the vendor app's encoder edit on FREQ1 (selected_generator must be 0,
// which is the LORENZ_init default). Goes through the real HandleEncoderEvent
// path and then mirrors to the NT store, exactly as customUi_impl would.
void low_rents_encoder_edit_freq1(_NT_algorithm* self, int delta) {
    auto* inst = static_cast<LorenzInstance*>(self);
    oc_runtime::emit_encoder(*inst, OC::CONTROL_ENCODER_L, delta);
    oc_runtime::push_settings_to_params(*inst);
}

// Drive an edit of an arbitrary list setting through the on-device path: place
// the cursor on setting_idx, enter editing mode, emit ENCODER_R, then push back.
// This is the path the user exercises with the hardware encoder, distinct from
// the ENCODER_L FREQ edit above. Used to prove a push-back edit lands on the
// edited setting and not its neighbor.
void low_rents_encoder_edit_setting(_NT_algorithm* self, int setting_idx, int delta) {
    auto* inst = static_cast<LorenzInstance*>(self);
    auto& cur = lorenz_generator_state.cursor;
    cur.Init(LORENZ_SETTING_RHO1, LORENZ_SETTING_LAST - 1);
    cur.Scroll(setting_idx - LORENZ_SETTING_RHO1);
    cur.set_editing(true);
    oc_runtime::emit_encoder(*inst, OC::CONTROL_ENCODER_R, delta);
    oc_runtime::push_settings_to_params(*inst);
}

void low_rents_arm_sentinel(_NT_algorithm* self) {
    static_cast<LorenzInstance*>(self)->alive = true;
}
