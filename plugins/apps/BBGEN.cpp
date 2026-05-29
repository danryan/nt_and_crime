// BBGEN: O_C APP_BBGEN port (four peaks bouncing-ball envelope generators).
//
// First quad-channel OC::App. The vendor app object is the file-scope singleton
// QuadBouncingBalls `bbgen`, holding BouncingBall balls_[4]. Each ball is a
// SettingsBase<BouncingBall, BB_SETTING_LAST> with 11 settings. The NT plug-in
// exposes all 4*11 = 44 settings as flat parameter rows; the settings facade is
// a quad facade dispatching row idx to balls_[idx/11] setting idx%11. Names are
// channel-prefixed ("A Gravity" ... "D Hard reset") since the flat NT param page
// has no channel grouping (the vendor customUI keeps the A/B/C/D titlebar UX).
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
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "util/util_math.h"
#include "UI/ui_events.h"

#include "../../shim/include/oc_app_manifests/BBGEN.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

// Bind bare `menu::` (used by the vendor app body) to OC::menu without dragging
// OC::UI into global scope (see Low_rents.cpp for the rationale).
namespace menu = OC::menu;

#define ENABLE_APP_BBGEN 1
#include "APP_BBGEN.h"

namespace {

using ManifestNS = oc_app::BBGEN;

struct BBGENInstance : public oc_runtime::AppAlgorithm {};
BBGENInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_bbgen_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              BBGEN_init,
    /* storageSize */       BBGEN_storageSize,
    /* Save */              BBGEN_save,
    /* Restore */           BBGEN_restore,
    /* HandleAppEvent */    BBGEN_handleAppEvent,
    /* loop */              BBGEN_loop,
    /* DrawMenu */          BBGEN_menu,
    /* DrawScreensaver */   BBGEN_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(BBGEN_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(BBGEN_handleEncoderEvent),
    /* isr */               BBGEN_isr,
};

// Channel-prefixed parameter names. Filled once at construct. The NT parameter
// .name pointer must outlive construct; this file-scope static satisfies it.
// Multiple instances share it (names are identical), so no per-instance state.
constexpr int kNumChannels = 4;
constexpr int kNumSettings = kNumChannels * BB_SETTING_LAST;  // 44
char g_names[kNumSettings][16];

void build_names() {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int s = 0; s < BB_SETTING_LAST; ++s) {
            const int i = ch * BB_SETTING_LAST + s;
            const char* vn = BouncingBall::value_attr(static_cast<size_t>(s)).name;
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

// The quad facade. instance is the vendor `bbgen` singleton; the lambdas are
// captureless (they reference file-scope globals: bbgen and the BBGEN_* quad
// persistence thunks), so each is a plain function pointer.
oc_runtime::SettingsFacade make_quad_facade() {
    oc_runtime::SettingsFacade f;
    f.instance = &bbgen;
    f.num_settings = kNumSettings;  // overwritten by construct_with_facade
    f.get_value = [](void* self, int idx) -> int {
        return static_cast<QuadBouncingBalls*>(self)
            ->balls_[idx / BB_SETTING_LAST]
            .get_value(static_cast<size_t>(idx % BB_SETTING_LAST));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        return static_cast<QuadBouncingBalls*>(self)
            ->balls_[idx / BB_SETTING_LAST]
            .apply_value(static_cast<size_t>(idx % BB_SETTING_LAST), value);
    };
    f.save = [](void* /*self*/, void* blob) -> size_t { return BBGEN_save(blob); };
    f.restore = [](void* /*self*/, const void* blob) -> size_t { return BBGEN_restore(blob); };
    f.storage_size = []() -> size_t { return BBGEN_storageSize(); };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &BouncingBall::value_attr(static_cast<size_t>(idx % BB_SETTING_LAST));
    };
    f.param_name = [](void* /*self*/, int idx) -> const char* {
        return g_names[idx];
    };
    return f;
}

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // 12 I/O routing rows + 44 settings (CLAUDE.md numParameters gotcha).
    req.numParameters = oc_runtime::kIoParamCount + kNumSettings;  // 56
    req.sram = sizeof(BBGENInstance);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) BBGENInstance();
    g_instance = inst;
    build_names();
    // BBGEN_init() (app->Init, fired inside construct) calls bbgen.Init(), which
    // InitDefaults() every ball. Wire the quad facade and build the parameter
    // table (I/O routing + 44 settings), then seed v[] from post-default values.
    oc_runtime::construct_with_facade(*inst, &the_bbgen_app, make_quad_facade(),
                                      kNumSettings);
    return inst;
}

void emit_button(const BBGENInstance* inst, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void emit_encoder(const BBGENInstance* inst, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void push_settings_to_params(BBGENInstance* inst) {
    if (!inst->alive) return;
    const int32_t idx = NT_algorithmIndex(inst);
    if (idx < 0) return;
    const int base = oc_runtime::settings_param_base();
    const int n = inst->settings_facade.num_settings;
    for (int s = 0; s < n; ++s) {
        const int v = inst->settings_facade.get_value(inst->settings_facade.instance, s);
        if (inst->v[base + s] != static_cast<int16_t>(v)) {
            // NT_setParameterFromUi takes the GLOBAL index: add
            // NT_parameterOffset() (CLAUDE.md offset gotcha).
            NT_setParameterFromUi(static_cast<uint32_t>(idx),
                                  static_cast<uint32_t>(base + s) + NT_parameterOffset(),
                                  static_cast<int16_t>(v));
        }
    }
}

void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    auto* inst = static_cast<BBGENInstance*>(self);
    if (!inst->app) return;

    int n = 0;
    const oc_runtime::ControlMapping* tbl = oc_runtime::button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;
    for (int i = 0; i < n; ++i) {
        const uint16_t bit = tbl[i].nt_bit;
        const int bi = oc_runtime::bit_index(bit);
        const bool now_down = (data.controls & bit) != 0;
        if ((edges & bit) && !now_down) {
            const uint8_t ev = oc_runtime::classify_release(inst, bi);
            emit_button(inst, tbl[i].oc_control, ev);
        }
    }
    if (data.encoders[0] != 0) emit_encoder(inst, OC::CONTROL_ENCODER_L, data.encoders[0]);
    if (data.encoders[1] != 0) emit_encoder(inst, OC::CONTROL_ENCODER_R, data.encoders[1]);

    push_settings_to_params(inst);
    oc_runtime::customUi(*inst, data);
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
// Test seams. The vendor QuadBouncingBalls type and `bbgen` singleton are only
// visible in this TU.
// ---------------------------------------------------------------------------
int bbgen_get_setting(int channel, int setting) {
    return bbgen.balls_[channel].get_value(static_cast<size_t>(setting));
}
bool bbgen_apply_setting(int channel, int setting, int value) {
    return bbgen.balls_[channel].apply_value(static_cast<size_t>(setting), value);
}
int bbgen_setting_count() { return kNumSettings; }
int bbgen_settings_per_channel() { return BB_SETTING_LAST; }
int bbgen_settings_param_base() { return oc_runtime::settings_param_base(); }
const char* bbgen_param_name(int idx) { build_names(); return g_names[idx]; }
void bbgen_arm_sentinel(_NT_algorithm* self) {
    static_cast<BBGENInstance*>(self)->alive = true;
}
