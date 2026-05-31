// Passencore: O_C APP_PASSENCORE port. A voice-leading chord sequencer. Trigger
// inputs sample a new target chord, play it, or play a passing chord that voice-
// leads between the current and target harmony; the four chord voices are
// emitted as 1V/oct pitch on the four DAC channels. The functional/neo-Riemannian
// scoring, the voicing search, the menu and screensaver draw code, and the ISR
// all compile unmodified from the vendor header.
//
// One NT plug-in compiling to one small .o. It embeds the vendor OC::App thunk
// table built from the PASSENCORE_* thunks, points OC::apps::current_app at it,
// and drives it through the shared per-app runtime.
//
// Structure follows plugins/apps/Harrington1200.cpp (the single-instance
// template): the vendor app owns the file-scope `passencore_instance` singleton
// (itself a SettingsBase) plus a separate `passencore_state` holding the customUI
// state (scale editor, menu cursor, the pending left-encoder scale value).
//
// Settings model (subset, MASK excluded): 17 of the 18 settings are NT
// parameters. PASSENCORE_SETTING_MASK (index 1) is STORAGE_TYPE_U16 with range
// 1..65535; _NT_parameter and _NT_algorithm::v are int16_t (max 32767), so the
// mask cannot be a parameter without truncation and corruption (the FPART U32
// lesson). It is also a 12-bit scale mask edited through the vendor scale-editor
// customUI, not as a single spinbox, so it stays app-internal. MASK sits at
// index 1 (mid-array, not the tail), so the facade remaps logical row i to
// physical setting `i >= 1 ? i + 1 : i`. The mask still persists: it lives in the
// same SettingsBase whose Save() serialises all 18 values regardless of
// num_settings, so the default make_facade blob hooks cover it (no override).

// Aggregation trigger: defining this BEFORE the runtime include pulls the OC
// shim impl into this single TU (see _per_app_runtime.h).
#define NT_OC_APP_TU 1

// Pull the vendor UI event type FIRST, before the runtime includes OC_apps.h.
// PASSENCORE instantiates a vendor UI editor (OC::ScaleEditor, a member of
// PassencoreState) whose template body does member access on `UI::Event` inside
// namespace OC. Including ui_events.h up front defines UI_EVENTS_H_, so the shim
// OC_apps.h aliases OC::UI to the global ::UI (see OC_apps.h): OC::UI::Event then
// IS ::UI::Event, the editor compiles against the complete type, and the vendor
// app can pass its ::UI::Event into the editor. Apps without a vendor UI editor
// include ui_events.h after the runtime and keep the incomplete-forward-decl
// regime; the ordering here is what selects the alias.
#include "UI/ui_events.h"

#include "_per_app_runtime.h"
#include "oc_customui_dispatch.h"

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
#include "OC_scales.h"            // OC::scale_names, the quantizer, the scale data
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "UI/ui_events.h"          // ::UI::Event for the customUi emit glue

#include "../../shim/include/oc_app_manifests/PASSENCORE.h"

#include <distingnt/api.h>
#include <new>

// Bind bare `menu::` (the vendor app uses menu::SettingsList / menu::TitleBar /
// menu::ScreenCursor) to the shim's OC::menu without dragging OC::UI into global
// scope (which would make the vendor app's `UI::Event` ambiguous against ::UI
// from ui_events.h).
namespace menu = OC::menu;

// Enable the vendor app body and pull it in. APP_PASSENCORE.h is guarded by
// ENABLE_APP_PASSENCORE; defining it before the include compiles the PASSENCORE
// settings class, the PassencoreState class, the file-scope `passencore_instance`
// / `passencore_state` singletons, and the PASSENCORE_* thunks into this TU.
#define ENABLE_APP_PASSENCORE 1
#include "APP_PASSENCORE.h"

namespace {

using ManifestNS = oc_app::PASSENCORE;

struct PassencoreInstance : public oc_runtime::AppAlgorithm {};

PassencoreInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

// The App aggregate. Field order matches OC::App (OC_apps.h).
const OC::App the_passencore_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              PASSENCORE_init,
    /* storageSize */       PASSENCORE_storageSize,
    /* Save */              PASSENCORE_save,
    /* Restore */           PASSENCORE_restore,
    /* HandleAppEvent */    PASSENCORE_handleAppEvent,
    /* loop */              PASSENCORE_loop,
    /* DrawMenu */          PASSENCORE_menu,
    /* DrawScreensaver */   PASSENCORE_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(PASSENCORE_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(PASSENCORE_handleEncoderEvent),
    /* isr */               PASSENCORE_isr,
};

// The one excluded setting (the U16 scale mask). Compile-time constant, so the
// remap stays expressible in captureless facade lambdas.
constexpr int kSkip = PASSENCORE_SETTING_MASK;  // 1

// Logical NT-parameter row -> physical PASSENCORE setting, skipping MASK.
constexpr int phys_setting(int logical) {
    return logical >= kSkip ? logical + 1 : logical;
}

// 17 of 18 settings are exposed (MASK excluded).
constexpr int kNumParamSettings = PASSENCORE_SETTING_LAST - 1;

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing
    // rows plus one row per exposed setting (17, NOT all 18).
    req.numParameters = oc_runtime::kIoParamCount + kNumParamSettings;
    req.sram          = sizeof(PassencoreInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) PassencoreInstance();
    g_instance = inst;

    // Build the standard single-instance facade on passencore_instance, then
    // override get/apply/value_attr with the MASK-skipping remap so the 17
    // exposed rows are a contiguous logical range over a non-contiguous physical
    // subset. save/restore/storage_size keep the make_facade defaults: PASSENCORE
    // is a SettingsBase whose Save() serialises all 18 values, so MASK persists
    // through the blob even though it is not a parameter.
    oc_runtime::SettingsFacade facade =
        oc_runtime::make_facade(&passencore_instance);
    facade.get_value = [](void* self, int i) -> int {
        return static_cast<PASSENCORE*>(self)->get_value(
            static_cast<size_t>(phys_setting(i)));
    };
    facade.apply_value = [](void* self, int i, int v) -> bool {
        return static_cast<PASSENCORE*>(self)->apply_value(
            static_cast<size_t>(phys_setting(i)), v);
    };
    facade.value_attr_at = [](int i) -> const settings::value_attr* {
        return &PASSENCORE::value_attr(static_cast<size_t>(phys_setting(i)));
    };

    oc_runtime::construct_with_facade(*inst, &the_passencore_app, facade,
                                      kNumParamSettings);
    return inst;
}

// Field order follows _NT_factory (api.h:468): tags BEFORE hasCustomUi/customUi.
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
    // The vendor handleButtonEvent reads only UI::EVENT_BUTTON_PRESS (no long
    // press), so no LONG_RELEASE -> LONG_PRESS mapping is needed.
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
// Test seams. The vendor PASSENCORE type and the `passencore_instance` singleton
// are only fully visible in this TU. The setting seams take a LOGICAL row index
// (the exposed-parameter numbering) and remap to the physical setting, so a test
// addresses the 17 exposed rows the same way the NT parameter table does.
// ---------------------------------------------------------------------------

int pc_get_setting(_NT_algorithm* /*self*/, int logical) {
    return passencore_instance.get_value(static_cast<size_t>(phys_setting(logical)));
}
bool pc_apply_setting(_NT_algorithm* /*self*/, int logical, int value) {
    return passencore_instance.apply_value(static_cast<size_t>(phys_setting(logical)), value);
}
int pc_setting_count() { return kNumParamSettings; }
int pc_settings_param_base() { return oc_runtime::settings_param_base(); }

// The excluded U16 scale mask, addressed directly (it is not a parameter row).
int  pc_get_mask() { return passencore_instance.get_value(PASSENCORE_SETTING_MASK); }
void pc_set_mask(int value) {
    passencore_instance.apply_value(PASSENCORE_SETTING_MASK, value);
}

// The four chord voices as the DAC codes set by play_chord -> set_pitch. Reads
// the live DAC backing the runtime flushes to the output buses.
void pc_get_outputs(int out[4]) {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<int>(OC::DAC::value(static_cast<size_t>(i)));
}

void pc_arm_sentinel(_NT_algorithm* self) {
    static_cast<PassencoreInstance*>(self)->alive = true;
}
