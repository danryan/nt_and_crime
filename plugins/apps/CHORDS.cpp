// Chords: O_C APP_CHORDS port. A four-voice chord sequencer. A scale plus a
// 12-bit active-note mask quantizes a root note; a user-defined chord progression
// then voices it as a four-note chord emitted as 1V/oct pitch on the four DAC
// channels. Triggers advance the progression (forward / reverse / pendulum /
// random / brownian) and a rich CV-mapping page can steer root, mask, transpose,
// octave, chord quality, voicing, inversion, progression slot, direction,
// brownian probability, and the per-progression chord count. The quantizer, the
// chord-voicing engine, the scale + chord editors, the menu / screensaver draw
// code, and the ISR all compile unmodified from the vendor header.
//
// One NT plug-in compiling to one small .o. It embeds the vendor OC::App thunk
// table built from the CHORDS_* thunks and drives it through the shared per-app
// runtime.
//
// Structure follows plugins/apps/PASSENCORE.cpp (the single-instance template):
// the vendor app owns the file-scope `chords` singleton (itself a SettingsBase)
// plus a separate `chords_state` (ChordQuantizer) holding the customUI state (the
// scale editor, the chord editor, the menu cursor, the pending left-encoder scale
// value).
//
// Settings model (subset, MASK excluded): 30 of the 31 settings are NT
// parameters. CHORDS_SETTING_MASK (index 3) is STORAGE_TYPE_U16 with range
// 1..65535; _NT_parameter and _NT_algorithm::v are int16_t (max 32767), so the
// mask cannot be a parameter without truncation and corruption (the FPART U32 /
// PASSENCORE lesson). It is also a 12-bit scale mask edited through the vendor
// scale-editor customUI, not a single spinbox, so it stays app-internal. MASK
// sits at index 3 (mid-array), so the facade remaps logical row i to physical
// setting `i >= 3 ? i + 1 : i`. The mask still persists: it lives in the same
// SettingsBase whose Save() serialises all 31 values regardless of num_settings,
// so the default make_facade blob hooks cover it (no override).
//
// Fidelity limit: the user chord-slot definitions live in the vendor global
// `OC::user_chords[]`, NOT in the `chords` SettingsBase. The vendor app's own
// blob (CHORDS_save == chords.Save) does NOT serialise them either (on hardware
// they are a separate global EEPROM page, which the NT has no equivalent of), so
// the NT port matches the vendor blob exactly: the chord editor works in-session,
// and chord-slot edits do not survive a preset reload. Documented, not a defect.

// Aggregation trigger: defining this BEFORE the runtime include pulls the OC shim
// impl into this single TU (see _per_app_runtime.h).
#define NT_OC_APP_TU 1

// Pull the vendor UI event type FIRST, before the runtime includes OC_apps.h.
// CHORDS instantiates vendor UI editors (OC::ScaleEditor<Chords> and
// OC::ChordEditor<Chords>, members of ChordQuantizer) whose template bodies do
// member access on `UI::Event` inside namespace OC. Including ui_events.h up front
// defines UI_EVENTS_H_, so the shim OC_apps.h aliases OC::UI to the global ::UI
// (see OC_apps.h): OC::UI::Event then IS ::UI::Event, the editors compile against
// the complete type, and the vendor app can pass its ::UI::Event into them.
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

#include "../../shim/include/oc_app_manifests/CHORDS.h"

#include <distingnt/api.h>
#include <new>

// Bind bare `menu::` (the vendor app uses menu::SettingsList / menu::DrawMask /
// menu::DrawChord) to the shim's OC::menu without dragging OC::UI into global
// scope (which would make the vendor app's `UI::Event` ambiguous against ::UI).
namespace menu = OC::menu;

// Vendor extern menu-redraw flag (on hardware OC_ui.cpp owns it). Same as the
// other ports; the per-app runtime redraws unconditionally.
uint_fast8_t MENU_REDRAW = 1;

// Enable the vendor app body and pull it in. APP_CHORDS.h is guarded by
// ENABLE_APP_CHORDS; defining it before the include compiles the Chords settings
// class, the ChordQuantizer state class, the file-scope `chords` / `chords_state`
// singletons, and the CHORDS_* thunks into this TU.
#define ENABLE_APP_CHORDS 1
#include "APP_CHORDS.h"

namespace {

using ManifestNS = oc_app::CHORDS;

struct ChordsInstance : public oc_runtime::AppAlgorithm {};

ChordsInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

// The App aggregate. Field order matches OC::App (OC_apps.h).
const OC::App the_chords_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              CHORDS_init,
    /* storageSize */       CHORDS_storageSize,
    /* Save */              CHORDS_save,
    /* Restore */           CHORDS_restore,
    /* HandleAppEvent */    CHORDS_handleAppEvent,
    /* loop */              CHORDS_loop,
    /* DrawMenu */          CHORDS_menu,
    /* DrawScreensaver */   CHORDS_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(CHORDS_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(CHORDS_handleEncoderEvent),
    /* isr */               CHORDS_isr,
};

// The one excluded setting (the U16 scale mask). Compile-time constant, so the
// remap stays expressible in captureless facade lambdas.
constexpr int kSkip = CHORDS_SETTING_MASK;  // 3

// Logical NT-parameter row -> physical CHORDS setting, skipping MASK.
constexpr int phys_setting(int logical) {
    return logical >= kSkip ? logical + 1 : logical;
}

// 30 of 31 settings are exposed (MASK excluded).
constexpr int kNumParamSettings = CHORDS_SETTING_LAST - 1;

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing rows
    // plus one row per exposed setting (30, NOT all 31).
    req.numParameters = oc_runtime::kIoParamCount + kNumParamSettings;
    req.sram          = sizeof(ChordsInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) ChordsInstance();
    g_instance = inst;

    // Build the standard single-instance facade on `chords`, then override
    // get/apply/value_attr with the MASK-skipping remap so the 30 exposed rows are
    // a contiguous logical range over a non-contiguous physical subset.
    // save/restore/storage_size keep the make_facade defaults: Chords is a
    // SettingsBase whose Save() serialises all 31 values, so MASK persists through
    // the blob even though it is not a parameter.
    oc_runtime::SettingsFacade facade = oc_runtime::make_facade(&chords);
    facade.get_value = [](void* self, int i) -> int {
        return static_cast<Chords*>(self)->get_value(
            static_cast<size_t>(phys_setting(i)));
    };
    facade.apply_value = [](void* self, int i, int v) -> bool {
        return static_cast<Chords*>(self)->apply_value(
            static_cast<size_t>(phys_setting(i)), v);
    };
    facade.value_attr_at = [](int i) -> const settings::value_attr* {
        return &Chords::value_attr(static_cast<size_t>(phys_setting(i)));
    };

    oc_runtime::construct_with_facade(*inst, &the_chords_app, facade,
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
    // The vendor handleButtonEvent and both editors (scale + chord) read
    // EVENT_BUTTON_LONG_PRESS, so the runtime LONG_RELEASE maps to vendor
    // LONG_PRESS.
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
// Test seams. The vendor Chords type and the `chords` singleton are only fully
// visible in this TU. The setting seams take a LOGICAL row index (the exposed-
// parameter numbering) and remap to the physical setting, so a test addresses the
// 30 exposed rows the same way the NT parameter table does.
// ---------------------------------------------------------------------------

int ch_get_setting(_NT_algorithm* /*self*/, int logical) {
    return chords.get_value(static_cast<size_t>(phys_setting(logical)));
}
bool ch_apply_setting(_NT_algorithm* /*self*/, int logical, int value) {
    return chords.apply_value(static_cast<size_t>(phys_setting(logical)), value);
}
int ch_setting_count() { return kNumParamSettings; }                 // 30
int ch_settings_total() { return CHORDS_SETTING_LAST; }              // 31
int ch_settings_param_base() { return oc_runtime::settings_param_base(); }
int ch_phys_setting(int logical) { return phys_setting(logical); }
int ch_mask_index() { return CHORDS_SETTING_MASK; }                  // 3

// The excluded U16 scale mask, addressed directly (it is not a parameter row).
int  ch_get_mask() { return chords.get_value(CHORDS_SETTING_MASK); }
void ch_set_mask(int value) { chords.apply_value(CHORDS_SETTING_MASK, value); }

// The four chord voices as the DAC codes the ISR sets. Reads the live DAC backing
// the runtime flushes to the output buses.
void ch_get_outputs(int out[4]) {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<int>(OC::DAC::value(static_cast<size_t>(i)));
}

void ch_arm_sentinel(_NT_algorithm* self) {
    static_cast<ChordsInstance*>(self)->alive = true;
}
