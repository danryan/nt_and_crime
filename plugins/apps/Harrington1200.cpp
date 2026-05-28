// Harrington 1200: O_C APP_H1200 port (neo-Riemannian tonnetz triad
// transformer). The heaviest of the two foundation validation apps and the
// .text budget canary.
//
// One NT plug-in compiling to one small .o. It embeds exactly one vendor
// OC::App thunk table built from the H1200_* thunks the vendor header defines,
// points OC::apps::current_app at it, and drives it from the NT plug-in entry
// points through the shared per-app runtime. The vendor app body (H1200_isr,
// H1200_clock, the menu/screensaver draw code, the event handlers, the
// H1200Settings/H1200State classes) compiles unmodified.
//
// Structure follows plugins/apps/Low_rents.cpp (the canonical real-app
// template). Like Low-rents, the vendor app owns file-scope singletons
// (`h1200_settings`, the SettingsBase instance, plus `h1200_state`), so the
// settings facade points at the vendor `h1200_settings` singleton rather than
// an instance-embedded settings object.

// Aggregation trigger: defining this BEFORE the runtime include pulls the OC
// shim impl into this single TU (see _per_app_runtime.h). The per-app host
// test TU does NOT define this, so only this .cpp aggregates and the shim
// globals are defined exactly once per linked binary.
#define NT_OC_APP_TU 1

#include "_per_app_runtime.h"

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "OC_config.h"
#include "OC_strings.h"
#include "OC_menus.h"
#include "OC_bitmaps.h"
#include "OC_scales.h"            // OC::SemitoneQuantizer (vendor, not shadowed)
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "UI/ui_events.h"          // ::UI::Event for the customUi emit glue

#include "../../shim/include/oc_app_manifests/Harrington1200.h"

#include <distingnt/api.h>
#include <new>

// The vendor app body reaches the hand-ported menu widgets as bare `menu::`.
// The shim lays them out in OC::menu (OC_menus.h). A full `using namespace OC`
// would also drag OC::UI (forward-declared in OC_apps.h) into the global scope
// and make the vendor app's `UI::Event` ambiguous against the real ::UI from
// ui_events.h. A namespace alias is surgical: it binds bare `menu::` to OC::menu
// without touching any other name. Everything else the vendor app uses is either
// OC-qualified (OC::ADC, OC::DAC, OC::DigitalInputs, OC::TriggerDelays,
// OC::SemitoneQuantizer, OC::visualize_pitch_classes, OC::Strings, OC::CONTROL_*)
// or global (graphics, CONSTRAIN, EuclideanFilter, tonnetz::*, note_name,
// ::UI::Event).
namespace menu = OC::menu;

// The vendor app header declares `extern uint_fast8_t MENU_REDRAW;`
// (APP_H1200.h:36) and writes it from H1200_clock to flag the firmware menu
// to repaint. On hardware OC_ui.cpp owns the definition; the shim does not
// compile that TU, so define the flag here. The per-app runtime redraws every
// draw() unconditionally, so the value is otherwise unobserved.
uint_fast8_t MENU_REDRAW = 1;

// Enable the vendor app body and pull it in. APP_H1200.h is guarded by
// ENABLE_APP_H1200; defining it before the include compiles the H1200Settings
// settings class, the file-scope `h1200_settings` / `h1200_state` singletons,
// and the H1200_* thunks into this TU.
#define ENABLE_APP_H1200 1
#include "APP_H1200.h"

namespace {

using ManifestNS = oc_app::Harrington1200;

// The runtime AppAlgorithm instance. The vendor app keeps all of its state
// (the h1200_settings singleton, h1200_state, the menu cursor) at file scope,
// so the instance only carries the runtime base. The settings facade points at
// the vendor `h1200_settings` singleton.
struct H1200Instance : public oc_runtime::AppAlgorithm {};

// The single live instance pointer, set in construct(). The OC::App thunks are
// the vendor file-scope statics; they reach the vendor singletons directly, so
// this pointer only services the test seams and the customUi push-back.
H1200Instance* g_instance = nullptr;

// OC::App declares HandleButtonEvent / HandleEncoderEvent as
// void(*)(const OC::UI::Event&) because OC_apps.h only forward-declares
// OC::UI::Event. The vendor thunks take the real top-level ::UI::Event (from
// ui_events.h). The two event types are layout-identical (the foundation's
// documented bridge assumption; Low_rents/StubApp reinterpret_cast the event
// reference across the same boundary), so the event-handler function pointers
// are bridged with a reinterpret_cast here. This is the symmetric counterpart
// of the reinterpret_cast in emit_button / emit_encoder below.
using OcEventFn = void (*)(const OC::UI::Event&);

// The App aggregate. Field order matches OC::App (OC_apps.h) and the vendor
// DECLARE_APP expansion (OC_apps.cpp:83): id, name, then the eleven thunks.
const OC::App the_h1200_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              H1200_init,
    /* storageSize */       H1200_storageSize,
    /* Save */              H1200_save,
    /* Restore */           H1200_restore,
    /* HandleAppEvent */    H1200_handleAppEvent,
    /* loop */              H1200_loop,
    /* DrawMenu */          H1200_menu,
    /* DrawScreensaver */   H1200_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(H1200_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(H1200_handleEncoderEvent),
    /* isr */               H1200_isr,
};

// ---------------------------------------------------------------------------
// Factory thunks. The settings facade is bound to the vendor `h1200_settings`
// singleton, which IS a SettingsBase<H1200Settings, H1200_SETTING_LAST>.
// ---------------------------------------------------------------------------

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing
    // rows plus one row per settings entry (CLAUDE.md numParameters gotcha).
    req.numParameters = oc_runtime::kIoParamCount + H1200_SETTING_LAST;
    req.sram          = sizeof(H1200Instance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) H1200Instance();
    g_instance = inst;

    // H1200_init() (fired inside construct via app->Init) calls
    // h1200_settings.Init() (InitDefaults + update_enabled_settings) and
    // h1200_state.Init() (which inits the tonnetz state to a major triad).
    // Wire the facade to the vendor singleton and build the parameter table
    // (I/O routing + one row per setting), set current_app, fire
    // APP_EVENT_RESUME (which resets the tonnetz state), and seed v[] from the
    // post-default settings.
    oc_runtime::construct(*inst, &the_h1200_app, &h1200_settings,
                          H1200_SETTING_LAST);
    return inst;
}

// ---------------------------------------------------------------------------
// customUi emit glue (copied from Low_rents; the runtime owns the control-edge
// bookkeeping, the per-app TU constructs the concrete ::UI::Event and bridges
// it to the vendor OC::UI::Event the app handlers expect).
//
// Harrington 1200 handlers act on EVENT_BUTTON_PRESS for UP/DOWN/L/R, on
// EVENT_BUTTON_LONG_PRESS for L (InitDefaults), and on EVENT_ENCODER for
// ENCODER_L/R. Both can mutate the vendor settings (the encoders edit
// inversion or the cursor's setting). After dispatching, the push-back step
// mirrors any changed setting into the NT parameter store via
// NT_setParameterFromUi.
// ---------------------------------------------------------------------------

void emit_button(const H1200Instance* inst, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void emit_encoder(const H1200Instance* inst, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

// Push any setting whose app-side value diverged from the NT parameter store
// back into the store. Guarded by the construct-time sentinel (alg.alive) and a
// valid algorithm index, matching the runtime's parameterChanged guard. The
// vendor event handlers edit h1200_settings.values_[] directly; this mirrors
// the post-event state into alg->v via NT_setParameterFromUi so the NT
// parameter page stays in sync with the customUI.
void push_settings_to_params(H1200Instance* inst) {
    if (!inst->alive) return;
    const int32_t idx = NT_algorithmIndex(inst);
    if (idx < 0) return;
    const int base = oc_runtime::settings_param_base();
    const int n    = inst->settings_facade.num_settings;
    for (int s = 0; s < n; ++s) {
        const int v = inst->settings_facade.get_value(
            inst->settings_facade.instance, s);
        if (inst->v[base + s] != static_cast<int16_t>(v)) {
            NT_setParameterFromUi(static_cast<uint32_t>(idx),
                                  static_cast<uint32_t>(base + s),
                                  static_cast<int16_t>(v));
        }
    }
}

void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    auto* inst = static_cast<H1200Instance*>(self);
    if (!inst->app) return;

    int n = 0;
    const oc_runtime::ControlMapping* tbl = oc_runtime::button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;

    // Buttons: emit on the release edge, classified short vs long by the
    // runtime. The long-release classification carries the vendor
    // EVENT_BUTTON_LONG_PRESS semantics Harrington 1200 reads on CONTROL_BUTTON_L.
    for (int i = 0; i < n; ++i) {
        const uint16_t bit  = tbl[i].nt_bit;
        const int      bi   = oc_runtime::bit_index(bit);
        const bool now_down = (data.controls & bit) != 0;
        if ((edges & bit) && !now_down) {
            const uint8_t ev = oc_runtime::classify_release(inst, bi);
            // A long release maps to the vendor LONG_PRESS the app handler
            // tests for; a short release maps to PRESS.
            const uint8_t mapped = (ev == oc_runtime::EVENT_BUTTON_LONG_RELEASE)
                                       ? oc_runtime::EVENT_BUTTON_LONG_PRESS
                                       : oc_runtime::EVENT_BUTTON_PRESS;
            emit_button(inst, tbl[i].oc_control, mapped);
        }
    }

    // Encoders: one EVENT_ENCODER per non-zero delta.
    if (data.encoders[0] != 0) {
        emit_encoder(inst, OC::CONTROL_ENCODER_L, data.encoders[0]);
    }
    if (data.encoders[1] != 0) {
        emit_encoder(inst, OC::CONTROL_ENCODER_R, data.encoders[1]);
    }

    // Mirror any app-side setting edit back into the NT parameter store.
    push_settings_to_params(inst);

    // Advance the runtime bookkeeping AFTER emitting so held_since/last_controls
    // reflect the post-event state.
    oc_runtime::customUi(*inst, data);
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
// Test seams. Defined here because the vendor H1200Settings type, the
// `h1200_settings` singleton, and the `h1200_state` tonnetz state are only
// fully visible in this TU. The host test reaches the settings and the tonnetz
// outputs through these without pulling the vendor header (and its
// SETTINGS_DECLARE specialization) into its own TU.
// ---------------------------------------------------------------------------
int h1200_get_setting(_NT_algorithm* /*self*/, int idx) {
    return h1200_settings.get_value(static_cast<size_t>(idx));
}
bool h1200_apply_setting(_NT_algorithm* /*self*/, int idx, int value) {
    return h1200_settings.apply_value(static_cast<size_t>(idx), value);
}
int h1200_setting_count() { return H1200_SETTING_LAST; }
int h1200_settings_param_base() { return oc_runtime::settings_param_base(); }

// Copy the four tonnetz outputs (rendered root + three triad voices) into
// out[4]. The host test asserts the neo-Riemannian transform result.
void h1200_get_outputs(_NT_algorithm* /*self*/, int out[4]) {
    h1200_state.tonnetz_state.get_outputs(out);
}

void h1200_arm_sentinel(_NT_algorithm* self) {
    static_cast<H1200Instance*>(self)->alive = true;
}
