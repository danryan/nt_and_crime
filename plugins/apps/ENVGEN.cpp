// ENVGEN: O_C APP_ENVGEN port ("Piqued" / "4x EG", four peaks multistage
// envelope generators with per-channel euclidean gating and a trigger-delay
// queue).
//
// Quad-channel OC::App. The vendor app object is the file-scope singleton
// QuadEnvelopeGenerator `envgen`, holding EnvelopeGenerator envelopes_[4]. Each
// channel is a SettingsBase<EnvelopeGenerator, ENV_SETTING_LAST> with 33
// settings. The NT plug-in exposes all 4*33 = 132 settings as flat parameter
// rows; the settings facade is the BBGEN quad facade dispatching row idx to
// envelopes_[idx/33] setting idx%33. Names are channel-prefixed ("A TYPE" ...
// "D Inverted") since the flat NT param page has no channel grouping (the vendor
// customUI keeps the A/B/C/D channel-select UX intact).
//
// Structure is byte-for-byte the BBGEN template (plugins/apps/BBGEN.cpp) with
// renamed thunks/types and the 33-vs-11 counts. Output is gate-triggered
// unipolar modulation (0V..+5V), like BBGEN's gated balls. No long-press read,
// so dispatch_custom_ui_factory<false>.
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
#include "OC_menus.h"            // menu::DrawMask (euclidean mask) must be in scope
#include "OC_strings.h"
#include "OC_bitmaps.h"
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "util/util_math.h"
#include "UI/ui_events.h"

#include "../../shim/include/oc_app_manifests/ENVGEN.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

// Bind bare `menu::` (used by the vendor app body and OC_euclidean_mask_draw.h)
// to OC::menu without dragging OC::UI into global scope (see Low_rents.cpp).
namespace menu = OC::menu;

// APP_ENVGEN.h is guarded by ENABLE_APP_PIQUED (the vendor app's original
// "Piqued" name), not ENABLE_APP_ENVGEN.
#define ENABLE_APP_PIQUED 1
#include "APP_ENVGEN.h"

namespace {

using ManifestNS = oc_app::ENVGEN;

struct ENVGENInstance : public oc_runtime::AppAlgorithm {};
ENVGENInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_envgen_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              ENVGEN_init,
    /* storageSize */       ENVGEN_storageSize,
    /* Save */              ENVGEN_save,
    /* Restore */           ENVGEN_restore,
    /* HandleAppEvent */    ENVGEN_handleAppEvent,
    /* loop */              ENVGEN_loop,
    /* DrawMenu */          ENVGEN_menu,
    /* DrawScreensaver */   ENVGEN_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(ENVGEN_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(ENVGEN_handleEncoderEvent),
    /* isr */               ENVGEN_isr,
};

// Channel-prefixed parameter names, filled once at construct. The NT parameter
// .name pointer must outlive construct; this file-scope static satisfies it.
constexpr int kNumChannels = ENVGEN_CHANNEL_COUNT;          // 4
constexpr int kNumSettings = kNumChannels * ENV_SETTING_LAST;  // 132
char g_names[kNumSettings][16];

void build_names() {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int s = 0; s < ENV_SETTING_LAST; ++s) {
            const int i = ch * ENV_SETTING_LAST + s;
            const char* vn = EnvelopeGenerator::value_attr(static_cast<size_t>(s)).name;
            char* dst = g_names[i];
            dst[0] = static_cast<char>('A' + ch);
            dst[1] = ' ';
            size_t len = std::strlen(vn);
            if (len > 13) len = 13;  // 16 - 2 prefix - 1 null
            std::memcpy(dst + 2, vn, len);
            dst[2 + len] = '\0';
        }
    }
}

// The quad facade. instance is the vendor `envgen` singleton; the lambdas are
// captureless (they reference file-scope globals: envgen and the ENVGEN_* quad
// persistence thunks), so each is a plain function pointer.
oc_runtime::SettingsFacade make_quad_facade() {
    oc_runtime::SettingsFacade f;
    f.instance = &envgen;
    f.num_settings = kNumSettings;  // overwritten by construct_with_facade
    f.get_value = [](void* self, int idx) -> int {
        return static_cast<QuadEnvelopeGenerator*>(self)
            ->envelopes_[idx / ENV_SETTING_LAST]
            .get_value(static_cast<size_t>(idx % ENV_SETTING_LAST));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        return static_cast<QuadEnvelopeGenerator*>(self)
            ->envelopes_[idx / ENV_SETTING_LAST]
            .apply_value(static_cast<size_t>(idx % ENV_SETTING_LAST), value);
    };
    f.save = [](void* /*self*/, void* blob) -> size_t { return ENVGEN_save(blob); };
    f.restore = [](void* /*self*/, const void* blob) -> size_t { return ENVGEN_restore(blob); };
    f.storage_size = []() -> size_t { return ENVGEN_storageSize(); };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &EnvelopeGenerator::value_attr(static_cast<size_t>(idx % ENV_SETTING_LAST));
    };
    f.param_name = [](void* /*self*/, int idx) -> const char* {
        return g_names[idx];
    };
    return f;
}

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // 12 I/O routing rows + 132 settings (CLAUDE.md numParameters gotcha).
    req.numParameters = oc_runtime::kIoParamCount + kNumSettings;  // 144
    req.sram = sizeof(ENVGENInstance);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) ENVGENInstance();
    g_instance = inst;
    build_names();
    // ENVGEN_init() (app->Init, fired inside construct) calls envgen.Init(),
    // which Init()s every channel (InitDefaults + default trigger DIGITAL_INPUT_
    // 1..4). Wire the quad facade, build the parameter table (I/O routing + 132
    // settings), then seed v[] from post-default values.
    oc_runtime::construct_with_facade(*inst, &the_envgen_app, make_quad_facade(),
                                      kNumSettings);
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
// Test seams. The vendor QuadEnvelopeGenerator type and `envgen` singleton are
// only visible in this TU.
// ---------------------------------------------------------------------------
int envgen_get_setting(int channel, int setting) {
    return envgen.envelopes_[channel].get_value(static_cast<size_t>(setting));
}
bool envgen_apply_setting(int channel, int setting, int value) {
    return envgen.envelopes_[channel].apply_value(static_cast<size_t>(setting), value);
}
int envgen_setting_count() { return kNumSettings; }
int envgen_settings_per_channel() { return ENV_SETTING_LAST; }
int envgen_settings_param_base() { return oc_runtime::settings_param_base(); }
const char* envgen_param_name(int idx) { build_names(); return g_names[idx]; }
void envgen_arm_sentinel(_NT_algorithm* self) {
    static_cast<ENVGENInstance*>(self)->alive = true;
}
