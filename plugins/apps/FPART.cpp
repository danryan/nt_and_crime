// FPART: O_C APP_FPART.h port ("4 Parts", a 4-voice chord-step sequencer with a
// staff-like display).
//
// One NT plug-in compiling to one small .o. It embeds exactly one vendor
// OC::App thunk table built from the FPART_* thunks the vendor header defines,
// points OC::apps::current_app at it, and drives it from the NT plug-in entry
// points through the shared per-app runtime. The vendor app body (FPART_isr,
// FPART_menu, the event handlers, the Fpart settings class) compiles unmodified.
//
// Structure follows plugins/apps/Harrington1200.cpp (which, like FPART, owns a
// file-scope SettingsBase singleton and maps a long-press button event). The
// one structural decision specific to FPART: the vendor app has 109 settings,
// but only the 10 int16-representable head settings (root, scale, loop bounds,
// the four voice octaves, the long-L toggle, the active-chord index) are
// exposed as NT parameters. The remaining 99 settings are U32 chord ints with a
// range up to 32323232, far beyond the int16_t the NT parameter store and
// _NT_algorithm::v use; exposing them as parameters would truncate and corrupt
// them. They stay app-internal, edited through the staff-page customUI (the
// vendor encoder handlers) and persisted whole through the Save/Restore blob.
// This matches the vendor app, whose own parameter menu draws only settings 0-9
// (APP_FPART.h:730).

// Aggregation trigger: defining this BEFORE the runtime include pulls the OC
// shim impl into this single TU (see _per_app_runtime.h). The per-app host test
// TU does NOT define this, so only this .cpp aggregates and the shim globals
// are defined exactly once per linked binary.
#define NT_OC_APP_TU 1

#include "_per_app_runtime.h"

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "OC_config.h"
#include "OC_menus.h"
#include "OC_strings.h"
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "util/util_math.h"
#include "UI/ui_events.h"          // ::UI::Event for the customUi emit glue

#include "../../shim/include/oc_app_manifests/FPART.h"

#include <distingnt/api.h>
#include <new>

// The vendor app body reaches the hand-ported menu widgets as bare `menu::`.
// The shim lays them out in OC::menu (OC_menus.h). A namespace alias binds bare
// `menu::` to OC::menu without dragging OC::UI (forward-declared in OC_apps.h)
// into the global scope, which would make the vendor app's `UI::Event`
// ambiguous against the real ::UI from ui_events.h. Everything else the vendor
// app uses is either OC-qualified or global (graphics, constrain, ::UI::Event).
namespace menu = OC::menu;

// Forward declarations for the button-handler helpers. The vendor
// FPART_handleButtonEvent (APP_FPART.h:748) calls these six free functions,
// which the vendor header defines further down (APP_FPART.h:851+). The Arduino
// IDE auto-generates prototypes for every function in an .ino/.h, so the vendor
// compiles despite the forward reference; this shim build has no auto-prototype
// pass, so the declarations are supplied here before the include. The vendor
// source is not edited.
void FPART_upButton();
void FPART_downButton();
void FPART_leftButton();
void FPART_rightButton();
void FPART_downButtonLong();
void FPART_leftButtonLong();

// Enable the vendor app body and pull it in. APP_FPART.h is guarded by
// ENABLE_APP_FPART; defining it before the include compiles the Fpart settings
// class, the file-scope `fpart_instance` singleton, and the FPART_* thunks into
// this TU. The thunks reach the vendor singleton directly, so the OC::App
// aggregate below must be built in this same TU.
#define ENABLE_APP_FPART 1
#include "APP_FPART.h"

namespace {

using ManifestNS = oc_app::FPART;

// Settings exposed as NT parameters: the 10 head settings (indices 0..9). The
// 99 chord ints (FPART_SETTING_CHORD0 == 10 onward) stay app-internal. The blob
// serialise/deserialise still covers all FPART_SETTING_LAST settings via the
// vendor SettingsBase::Save/Restore; only the NT parameter table is trimmed.
constexpr int kFpartHeadParams = FPART_SETTING_CHORD0;  // == 10

// The runtime AppAlgorithm instance. The vendor app keeps all of its state (the
// fpart_instance singleton, its cursor) at file scope, so the instance only
// carries the runtime base. The settings facade points at the vendor singleton.
struct FpartInstance : public oc_runtime::AppAlgorithm {};

// The single live instance pointer, set in construct(). The OC::App thunks are
// the vendor file-scope statics; they reach the vendor singleton directly, so
// this pointer only services the test seams and the customUi push-back.
FpartInstance* g_instance = nullptr;

// OC::App declares HandleButtonEvent / HandleEncoderEvent as
// void(*)(const OC::UI::Event&) because OC_apps.h only forward-declares
// OC::UI::Event. The vendor thunks take the real top-level ::UI::Event (from
// ui_events.h). The two event types are layout-identical (the foundation's
// documented bridge assumption), so the event-handler function pointers are
// bridged with a reinterpret_cast here, the symmetric counterpart of the
// reinterpret_cast in emit_button / emit_encoder below.
using OcEventFn = void (*)(const OC::UI::Event&);

// The App aggregate. Field order matches OC::App (OC_apps.h) and the vendor
// DECLARE_APP expansion: id, name, then the eleven thunks.
const OC::App the_fpart_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              FPART_init,
    /* storageSize */       FPART_storageSize,
    /* Save */              FPART_save,
    /* Restore */           FPART_restore,
    /* HandleAppEvent */    FPART_handleAppEvent,
    /* loop */              FPART_loop,
    /* DrawMenu */          FPART_menu,
    /* DrawScreensaver */   FPART_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(FPART_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(FPART_handleEncoderEvent),
    /* isr */               FPART_isr,
};

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing
    // rows plus one row per exposed head setting (CLAUDE.md numParameters
    // gotcha). The 99 chord ints are NOT in the table.
    req.numParameters = oc_runtime::kIoParamCount + kFpartHeadParams;
    req.sram          = sizeof(FpartInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) FpartInstance();
    g_instance = inst;

    // FPART_init() (fired inside construct via app->Init) runs
    // fpart_instance.Init() (InitDefaults + the cursor init). Wire the facade to
    // the vendor singleton with the head-setting count, build the parameter
    // table (I/O routing + one row per head setting), set current_app, and seed
    // v[] from the post-default head settings. The facade's save/restore still
    // cover all 109 settings (they call the vendor Save/Restore directly); only
    // num_settings (the parameter-table extent) is trimmed to the head count.
    oc_runtime::construct(*inst, &the_fpart_app, &fpart_instance,
                          kFpartHeadParams);
    return inst;
}

// ---------------------------------------------------------------------------
// customUi emit glue (mirrors Harrington1200.cpp; the runtime owns the
// control-edge bookkeeping, the per-app TU constructs the concrete ::UI::Event
// and bridges it to the vendor OC::UI::Event the app handlers expect).
//
// FPART handlers act on EVENT_BUTTON_PRESS for UP/DOWN/L/R, on
// EVENT_BUTTON_LONG_PRESS for DOWN (toggle staff/param page) and L (copy/paste
// or jump-to-zero), and on EVENT_ENCODER for ENCODER_L/R. On the staff page the
// encoders edit the active chord's note values (stored as a U32 chord int); on
// the parameter page ENCODER_R edits the cursor's head setting. After
// dispatching, the push-back mirrors any changed head setting into the NT
// parameter store via NT_setParameterFromUi.
// ---------------------------------------------------------------------------

void emit_button(const FpartInstance* inst, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void emit_encoder(const FpartInstance* inst, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

// Push any head setting whose app-side value diverged from the NT parameter
// store back into the store. Guarded by the construct-time sentinel (alg.alive)
// and a valid algorithm index, matching the runtime's parameterChanged guard.
// Only the head settings [0, num_settings) are mirrored; the chord ints are not
// parameters. The vendor event handlers edit fpart_instance.values_[] directly;
// this mirrors the post-event head state into alg->v via NT_setParameterFromUi.
void push_settings_to_params(FpartInstance* inst) {
    if (!inst->alive) return;
    const int32_t idx = NT_algorithmIndex(inst);
    if (idx < 0) return;
    const int base = oc_runtime::settings_param_base();
    const int n    = inst->settings_facade.num_settings;
    for (int s = 0; s < n; ++s) {
        const int v = inst->settings_facade.get_value(
            inst->settings_facade.instance, s);
        if (inst->v[base + s] != static_cast<int16_t>(v)) {
            // NT_setParameterFromUi indexes the GLOBAL parameter table, which
            // includes the firmware's common-parameter prefix. inst->v above is
            // plug-in-relative, so the store compare uses base + s, but the push
            // target must add NT_parameterOffset() (api.h:571). Omitting it
            // writes one global index low and the firmware re-applies the edit to
            // the setting above the edited one.
            NT_setParameterFromUi(static_cast<uint32_t>(idx),
                                  static_cast<uint32_t>(base + s) + NT_parameterOffset(),
                                  static_cast<int16_t>(v));
        }
    }
}

void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    auto* inst = static_cast<FpartInstance*>(self);
    if (!inst->app) return;

    int n = 0;
    const oc_runtime::ControlMapping* tbl = oc_runtime::button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;

    // Buttons: emit on the release edge, classified short vs long by the
    // runtime. A long release maps to the vendor EVENT_BUTTON_LONG_PRESS the
    // app handler tests for on DOWN and L; a short release maps to PRESS.
    for (int i = 0; i < n; ++i) {
        const uint16_t bit  = tbl[i].nt_bit;
        const int      bi   = oc_runtime::bit_index(bit);
        const bool now_down = (data.controls & bit) != 0;
        if ((edges & bit) && !now_down) {
            const uint8_t ev = oc_runtime::classify_release(inst, bi);
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

    // Mirror any app-side head setting edit back into the NT parameter store.
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
// Test seams. Defined here because the vendor Fpart type and the
// `fpart_instance` singleton are only fully visible in this TU. The host test
// reaches the full 109-setting table, the chord helpers, and the runtime view
// through these without pulling the vendor header (and its SETTINGS_DECLARE
// specialization) into its own TU.
// ---------------------------------------------------------------------------
int fpart_setting_count() { return FPART_SETTING_LAST; }
int fpart_head_param_count() { return kFpartHeadParams; }
int fpart_get_setting(_NT_algorithm* /*self*/, int idx) {
    return fpart_instance.get_value(static_cast<size_t>(idx));
}
bool fpart_apply_setting(_NT_algorithm* /*self*/, int idx, int value) {
    return fpart_instance.apply_value(static_cast<size_t>(idx), value);
}
int fpart_settings_param_base() { return oc_runtime::settings_param_base(); }

void fpart_set_chord(int chord_num, int a, int b, int c, int d) {
    fpart_instance.set_chord_int(chord_num,
                                 fpart_instance.build_chord_int(a, b, c, d));
}
int fpart_get_chord(int chord_num) {
    return fpart_instance.get_chord_int(chord_num);
}
void fpart_set_active_chord(int chord_num) {
    fpart_instance.set_activechord(chord_num);
}

void fpart_arm_sentinel(_NT_algorithm* self) {
    static_cast<FpartInstance*>(self)->alive = true;
}

// Drive an edit of a head setting through the on-device encoder path: switch to
// the parameter page, place the cursor on setting_idx, enter editing, emit
// ENCODER_R, then push back into the NT store, exactly as customUi_impl would.
// This is the path the user exercises with the hardware encoder on the
// parameter page, distinct from the staff-page chord edit.
void fpart_encoder_edit_setting(_NT_algorithm* self, int setting_idx, int delta) {
    auto* inst = static_cast<FpartInstance*>(self);
    fpart_instance.set_menu_page(FPART_MENU_PARAMETERS);
    fpart_instance.cursor.Init(FPART_SETTING_ROOT, FPART_SETTING_LAST - 1);
    fpart_instance.cursor.Scroll(setting_idx - FPART_SETTING_ROOT);
    fpart_instance.cursor.set_editing(true);
    emit_encoder(inst, OC::CONTROL_ENCODER_R, delta);
    push_settings_to_params(inst);
}
