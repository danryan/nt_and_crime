// POLYLFO: O_C APP_POLYLFO port ("Poly LFO", a quadrature wavetable LFO with
// four phase-related full-scale modulation outputs, based on the Mutable
// Instruments Frames easter egg).
//
// One NT plug-in compiling to one small .o. It embeds exactly one vendor
// OC::App thunk table built from the POLYLFO_* thunks the vendor header
// defines, points OC::apps::current_app at it, and drives it from the NT
// plug-in entry points through the shared per-app runtime. The vendor app body
// (POLYLFO_isr, POLYLFO_menu, the event handlers, the PolyLfo settings class,
// the file-scope poly_lfo singleton) compiles unmodified.
//
// Structure follows plugins/apps/Harrington1200.cpp: like H1200, the vendor app
// owns a file-scope SettingsBase singleton (`poly_lfo`) plus a state struct, so
// the settings facade points at the vendor singleton rather than an
// instance-embedded settings object, and it maps a long-press event
// (EVENT_BUTTON_LONG_PRESS on DOWN sets the phase-reset flag), so the customUi
// dispatch is parameterized <true>. All 21 non-VOR settings are int16-safe and
// become NT parameter rows (no FPART-style subsetting).

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
#include "OC_gpio.h"               // TR4 + digitalReadFast (free-run freq mult)
#include "OC_config.h"
#include "OC_strings.h"
#include "OC_menus.h"
#include "OC_bitmaps.h"            // OC::bitmap_indicator_4x8 (menu freq-mult flag)
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "util/util_math.h"
#include "UI/ui_events.h"          // ::UI::Event for the customUi emit glue

#include "../../shim/include/oc_app_manifests/POLYLFO.h"

#include <distingnt/api.h>
#include <new>

// The vendor app body reaches the hand-ported menu widgets as bare `menu::`.
// The shim lays them out in OC::menu (OC_menus.h). A namespace alias binds bare
// `menu::` to OC::menu without dragging OC::UI (forward-declared in OC_apps.h)
// into the global scope, which would make the vendor app's `UI::Event`
// ambiguous against the real ::UI from ui_events.h. Everything else the vendor
// app uses is either OC-qualified or global (graphics, CONSTRAIN, the SCALE8_16
// / USAT16 macros, ::UI::Event).
namespace menu = OC::menu;

// Enable the vendor app body and pull it in. APP_POLYLFO.h is guarded by
// ENABLE_APP_POLYLFO; defining it before the include compiles the PolyLfo
// settings class, the file-scope `poly_lfo` / `poly_lfo_state` singletons, the
// Frames engine glue, and the POLYLFO_* thunks into this TU. VOR is NOT defined,
// so VBiasManager.h reduces to its include guard, the app's saveVbias/
// restoreVbias compile out, and POLYLFO_SETTING_LAST == 21.
#define ENABLE_APP_POLYLFO 1
#include "APP_POLYLFO.h"

namespace {

using ManifestNS = oc_app::POLYLFO;

// The runtime AppAlgorithm instance. The vendor app keeps all of its state (the
// poly_lfo singleton, poly_lfo_state, the Frames engine) at file scope, so the
// instance only carries the runtime base. The settings facade points at the
// vendor `poly_lfo` singleton.
struct PolyLfoInstance : public oc_runtime::AppAlgorithm {};

// The single live instance pointer, set in construct(). The OC::App thunks are
// the vendor file-scope statics; they reach the vendor singleton directly, so
// this pointer only services the test seams and the customUi push-back.
PolyLfoInstance* g_instance = nullptr;

// OC::App declares HandleButtonEvent / HandleEncoderEvent as
// void(*)(const OC::UI::Event&) because OC_apps.h only forward-declares
// OC::UI::Event. The vendor thunks take the real top-level ::UI::Event (from
// ui_events.h). The two event types are layout-identical (the foundation's
// documented bridge assumption), so the event-handler function pointers are
// bridged with a reinterpret_cast here, the symmetric counterpart of the
// reinterpret_cast in emit_button / emit_encoder.
using OcEventFn = void (*)(const OC::UI::Event&);

// The App aggregate. Field order matches OC::App (OC_apps.h) and the vendor
// DECLARE_APP expansion: id, name, then the eleven thunks.
const OC::App the_polylfo_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              POLYLFO_init,
    /* storageSize */       POLYLFO_storageSize,
    /* Save */              POLYLFO_save,
    /* Restore */           POLYLFO_restore,
    /* HandleAppEvent */    POLYLFO_handleAppEvent,
    /* loop */              POLYLFO_loop,
    /* DrawMenu */          POLYLFO_menu,
    /* DrawScreensaver */   POLYLFO_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(POLYLFO_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(POLYLFO_handleEncoderEvent),
    /* isr */               POLYLFO_isr,
};

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing
    // rows plus one row per setting (CLAUDE.md numParameters gotcha). All 21
    // non-VOR settings are int16-safe, so the table is the full setting count.
    req.numParameters = oc_runtime::kIoParamCount + POLYLFO_SETTING_LAST;
    req.sram          = sizeof(PolyLfoInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) PolyLfoInstance();
    g_instance = inst;

    // POLYLFO_init() (fired inside construct via app->Init) runs
    // poly_lfo.Init() (InitDefaults + the Frames engine init) and the cursor
    // init. Wire the facade to the vendor singleton with the full setting
    // count, build the parameter table (I/O routing + one row per setting), set
    // current_app, and seed v[] from the post-default settings.
    oc_runtime::construct(*inst, &the_polylfo_app, &poly_lfo,
                          POLYLFO_SETTING_LAST);
    return inst;
}

// ---------------------------------------------------------------------------
// The factory. Field order follows _NT_factory (api.h:468): tags BEFORE
// hasCustomUi/customUi, serialise/deserialise after. See aeabi_probe.cpp.
// dispatch_custom_ui_factory<true> because the vendor reads
// EVENT_BUTTON_LONG_PRESS (APP_POLYLFO.h:499).
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
    .customUi              = oc_runtime::dispatch_custom_ui_factory<true>,
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
// Test seams. Defined here because the vendor PolyLfo type and the `poly_lfo`
// singleton are only fully visible in this TU. The host test reaches the
// settings, the engine DAC codes, and the runtime view through these without
// pulling the vendor header (and its SETTINGS_DECLARE specialization) into its
// own TU.
// ---------------------------------------------------------------------------
int polylfo_get_setting(_NT_algorithm* /*self*/, int idx) {
    return poly_lfo.get_value(static_cast<size_t>(idx));
}
bool polylfo_apply_setting(_NT_algorithm* /*self*/, int idx, int value) {
    return poly_lfo.apply_value(static_cast<size_t>(idx), value);
}
int polylfo_setting_count() { return POLYLFO_SETTING_LAST; }
int polylfo_settings_param_base() { return oc_runtime::settings_param_base(); }
int polylfo_get_dac_code(int channel) {
    return static_cast<int>(poly_lfo.lfo.dac_code(static_cast<uint8_t>(channel)));
}

void polylfo_arm_sentinel(_NT_algorithm* self) {
    static_cast<PolyLfoInstance*>(self)->alive = true;
}
