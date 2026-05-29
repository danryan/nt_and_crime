// BYTEBEATGEN: O_C APP_BYTEBEATGEN port (four peaks bytebeat generators).
//
// Quad-channel OC::App, sibling of BBGEN. The vendor app object is the file-scope
// singleton QuadByteBeats `bytebeatgen`, holding ByteBeat bytebeats_[4]. Each
// channel is a SettingsBase<ByteBeat, BYTEBEAT_SETTING_LAST> with 19 settings. The
// NT plug-in exposes all 4*19 = 76 settings as flat parameter rows (the first app
// to exceed the runtime kMaxSettings=64 cap, raised to 80). The settings facade is
// a quad facade dispatching row idx to bytebeats_[idx/19] setting idx%19. Names are
// channel-prefixed ("A Equation" ... "D CV4 -> ") since the flat NT param page has
// no channel grouping. The vendor customUI keeps the A/B/C/D titlebar plus the
// conditional (enabled-settings) loop-row hiding intact.
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
#include "util/util_misc.h"
#include "UI/ui_events.h"

#include "../../shim/include/oc_app_manifests/BYTEBEATGEN.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

// Bind bare `menu::` (used by the vendor app body) to OC::menu without dragging
// OC::UI into global scope (see Low_rents.cpp for the rationale).
namespace menu = OC::menu;

#define ENABLE_APP_BYTEBEATGEN 1
#include "APP_BYTEBEATGEN.h"

namespace {

using ManifestNS = oc_app::BYTEBEATGEN;

struct BYTEBEATGENInstance : public oc_runtime::AppAlgorithm {};
BYTEBEATGENInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_bytebeatgen_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              BYTEBEATGEN_init,
    /* storageSize */       BYTEBEATGEN_storageSize,
    /* Save */              BYTEBEATGEN_save,
    /* Restore */           BYTEBEATGEN_restore,
    /* HandleAppEvent */    BYTEBEATGEN_handleAppEvent,
    /* loop */              BYTEBEATGEN_loop,
    /* DrawMenu */          BYTEBEATGEN_menu,
    /* DrawScreensaver */   BYTEBEATGEN_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(BYTEBEATGEN_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(BYTEBEATGEN_handleEncoderEvent),
    /* isr */               BYTEBEATGEN_isr,
};

// Channel-prefixed parameter names. Filled once at construct. The NT parameter
// .name pointer must outlive construct; this file-scope static satisfies it.
// Multiple instances share it (names are identical), so no per-instance state.
constexpr int kNumChannels = 4;
constexpr int kNumSettings = kNumChannels * BYTEBEAT_SETTING_LAST;  // 76
char g_names[kNumSettings][16];

void build_names() {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int s = 0; s < BYTEBEAT_SETTING_LAST; ++s) {
            const int i = ch * BYTEBEAT_SETTING_LAST + s;
            const char* vn = ByteBeat::value_attr(static_cast<size_t>(s)).name;
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

// The quad facade. instance is the vendor `bytebeatgen` singleton; the lambdas are
// captureless (they reference file-scope globals: bytebeatgen and the BYTEBEATGEN_*
// quad persistence thunks), so each is a plain function pointer.
oc_runtime::SettingsFacade make_quad_facade() {
    oc_runtime::SettingsFacade f;
    f.instance = &bytebeatgen;
    f.num_settings = kNumSettings;  // overwritten by construct_with_facade
    f.get_value = [](void* self, int idx) -> int {
        return static_cast<QuadByteBeats*>(self)
            ->bytebeats_[idx / BYTEBEAT_SETTING_LAST]
            .get_value(static_cast<size_t>(idx % BYTEBEAT_SETTING_LAST));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        return static_cast<QuadByteBeats*>(self)
            ->bytebeats_[idx / BYTEBEAT_SETTING_LAST]
            .apply_value(static_cast<size_t>(idx % BYTEBEAT_SETTING_LAST), value);
    };
    f.save = [](void* /*self*/, void* blob) -> size_t { return BYTEBEATGEN_save(blob); };
    f.restore = [](void* /*self*/, const void* blob) -> size_t { return BYTEBEATGEN_restore(blob); };
    f.storage_size = []() -> size_t { return BYTEBEATGEN_storageSize(); };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &ByteBeat::value_attr(static_cast<size_t>(idx % BYTEBEAT_SETTING_LAST));
    };
    f.param_name = [](void* /*self*/, int idx) -> const char* {
        return g_names[idx];
    };
    return f;
}

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // 12 I/O routing rows + 76 settings (CLAUDE.md numParameters gotcha).
    req.numParameters = oc_runtime::kIoParamCount + kNumSettings;  // 88
    req.sram = sizeof(BYTEBEATGENInstance);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) BYTEBEATGENInstance();
    g_instance = inst;
    build_names();
    // BYTEBEATGEN_init() (app->Init, fired inside construct) calls
    // bytebeatgen.Init(), which InitDefaults() every channel. Wire the quad facade
    // and build the parameter table (I/O routing + 76 settings), then seed v[] from
    // post-default values.
    oc_runtime::construct_with_facade(*inst, &the_bytebeatgen_app, make_quad_facade(),
                                      kNumSettings);
    return inst;
}

void emit_button(const BYTEBEATGENInstance* inst, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void emit_encoder(const BYTEBEATGENInstance* inst, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  oc_runtime::last_controls_of(*inst));
    inst->app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

void push_settings_to_params(BYTEBEATGENInstance* inst) {
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
    auto* inst = static_cast<BYTEBEATGENInstance*>(self);
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
// Test seams. The vendor QuadByteBeats type and `bytebeatgen` singleton are only
// visible in this TU.
// ---------------------------------------------------------------------------
int bytebeatgen_get_setting(int channel, int setting) {
    return bytebeatgen.bytebeats_[channel].get_value(static_cast<size_t>(setting));
}
bool bytebeatgen_apply_setting(int channel, int setting, int value) {
    return bytebeatgen.bytebeats_[channel].apply_value(static_cast<size_t>(setting), value);
}
int bytebeatgen_setting_count() { return kNumSettings; }
int bytebeatgen_settings_per_channel() { return BYTEBEAT_SETTING_LAST; }
int bytebeatgen_settings_param_base() { return oc_runtime::settings_param_base(); }
const char* bytebeatgen_param_name(int idx) { build_names(); return g_names[idx]; }
int bytebeatgen_enabled_count(int channel) {
    bytebeatgen.bytebeats_[channel].update_enabled_settings();
    return bytebeatgen.bytebeats_[channel].num_enabled_settings();
}
void bytebeatgen_arm_sentinel(_NT_algorithm* self) {
    static_cast<BYTEBEATGENInstance*>(self)->alive = true;
}
