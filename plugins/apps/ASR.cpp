// Analog Shift Register: O_C APP_ASR port. A clocked four-stage quantizing shift
// register: each clock samples the (quantized) source, pushes it into a ring, and
// emits the four most recent samples on the four DAC outputs. The source can be a
// CV input, a Turing machine, a bytebeat, or an integer sequence. The quantizer,
// the scale/mask editor, the menu and screensaver draw code, and the ISR compile
// unmodified.
//
// Single-instance OC::App (the Harrington1200 / PASSENCORE template): the vendor
// owns the file-scope `asr` singleton (an ASRApp SettingsBase) plus `asr_state`
// holding the customUI state (scale editor, menu cursor, pending left-encoder
// scale value).
//
// Settings model (subset, MASK excluded): 26 of the 27 settings are NT
// parameters. ASR_SETTING_MASK (index 3) is STORAGE_TYPE_U16 (1..65535), which
// overflows int16 and is edited through the scale-editor customUI, so it stays
// app-internal. The facade remaps a logical row to physical `w < 3 ? w : w + 1`.
// The mask persists through the same SettingsBase Save (default make_facade blob
// hooks; ASR is single-instance, no override needed, like PASSENCORE).

#define NT_OC_APP_TU 1

// Pull the vendor UI event type FIRST (before the runtime includes OC_apps.h):
// ASR instantiates a vendor UI editor (OC::ScaleEditor<ASRApp>, a member of
// asr_state) whose template body does member access on UI::Event inside namespace
// OC. Defining UI_EVENTS_H_ up front makes the shim OC_apps.h alias OC::UI to
// ::UI (see OC_apps.h and the PASSENCORE precedent).
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
#include "OC_visualfx.h"
#include "OC_scales.h"
#include "OC_scale_edit.h"
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
// Pull the shim util/util_math.h shadow BEFORE APP_ASR.h. ASR uses SlewedValue
// and USAT16 (vendor util/util_math.h), which the shim shadows (poisoning
// UTIL_MATH_H_). The host test force-includes this header, but the ARM build
// does not, so include it explicitly here or the vendor sibling never lands and
// SlewedValue/USAT16 go undeclared on ARM.
#include "util/util_math.h"
#include "util/util_turing.h"

#include "../../shim/include/oc_app_manifests/ASR.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

namespace menu = OC::menu;

// Vendor extern menu-redraw flag (on hardware OC_ui.cpp owns it). Same as the
// other ports.
uint_fast8_t MENU_REDRAW = 1;

#define ENABLE_APP_ASR 1
#include "APP_ASR.h"

namespace {

using ManifestNS = oc_app::ASR;

struct ASRInstance : public oc_runtime::AppAlgorithm {};
ASRInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_asr_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              ASR_init,
    /* storageSize */       ASR_storageSize,
    /* Save */              ASR_save,
    /* Restore */           ASR_restore,
    /* HandleAppEvent */    ASR_handleAppEvent,
    /* loop */              ASR_loop,
    /* DrawMenu */          ASR_menu,
    /* DrawScreensaver */   ASR_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(ASR_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(ASR_handleEncoderEvent),
    /* isr */               ASR_isr,
};

// The one excluded setting (the U16 scale mask). Compile-time constant, so the
// remap stays expressible in captureless facade lambdas.
constexpr int kSkip = ASR_SETTING_MASK;  // 3
constexpr int phys_setting(int logical) {
    return logical >= kSkip ? logical + 1 : logical;
}
constexpr int kNumParamSettings = ASR_SETTING_LAST - 1;  // 26

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = oc_runtime::kIoParamCount + kNumParamSettings;
    req.sram          = sizeof(ASRInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) ASRInstance();
    g_instance = inst;

    // Standard single-instance facade on `asr`, with the MASK-skipping remap on
    // get/apply/value_attr. save/restore/storage_size keep the make_facade
    // defaults: ASRApp's Save serialises all 27 settings, so the mask persists.
    oc_runtime::SettingsFacade facade = oc_runtime::make_facade(&asr);
    facade.get_value = [](void* self, int i) -> int {
        return static_cast<ASRApp*>(self)->get_value(
            static_cast<size_t>(phys_setting(i)));
    };
    facade.apply_value = [](void* self, int i, int v) -> bool {
        return static_cast<ASRApp*>(self)->apply_value(
            static_cast<size_t>(phys_setting(i)), v);
    };
    facade.value_attr_at = [](int i) -> const settings::value_attr* {
        return &ASRApp::value_attr(static_cast<size_t>(phys_setting(i)));
    };

    oc_runtime::construct_with_facade(*inst, &the_asr_app, facade,
                                      kNumParamSettings);
    return inst;
}

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
    // The vendor handleEncoderEvent reads EVENT_BUTTON_LONG_PRESS, so the runtime
    // LONG_RELEASE maps to the vendor LONG_PRESS.
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
// Test seams. The vendor ASRApp type and the `asr` singleton are only visible in
// this TU. The setting seams take a LOGICAL row index (the exposed-parameter
// numbering) and remap to the physical setting.
// ---------------------------------------------------------------------------

int asr_get_setting(_NT_algorithm* /*self*/, int logical) {
    return asr.get_value(static_cast<size_t>(phys_setting(logical)));
}
bool asr_apply_setting(_NT_algorithm* /*self*/, int logical, int value) {
    return asr.apply_value(static_cast<size_t>(phys_setting(logical)), value);
}
int asr_setting_count() { return kNumParamSettings; }   // 26
int asr_settings_param_base() { return oc_runtime::settings_param_base(); }
// The excluded U16 scale mask, addressed directly.
int  asr_get_mask() { return asr.get_value(ASR_SETTING_MASK); }
void asr_set_mask(int value) { asr.apply_value(ASR_SETTING_MASK, value); }
// The four shift-register DAC outputs.
void asr_get_outputs(int out[4]) {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<int>(OC::DAC::value(static_cast<size_t>(i)));
}
void asr_arm_sentinel(_NT_algorithm* self) {
    static_cast<ASRInstance*>(self)->alive = true;
}
