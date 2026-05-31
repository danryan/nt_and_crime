// Quantermain: O_C APP_QQ port. Four independent quantizer channels
// (QuantizerChannel, each a SettingsBase of 51 settings), each with a scale +
// active-note mask and one of several sources (CV, Turing machine, logistic map,
// bytebeat, integer sequence). QQ_isr runs channel i's Update against DAC channel
// i, so each output is that channel's quantized 1V/oct pitch. The quantizer DSP,
// the scale/mask editor, the menu and screensaver draw code, and the ISR compile
// unmodified.
//
// Multi-channel facade port (the BBGEN/DQ quad-facade shape with N = 4). The
// vendor keeps the four channels in the file-scope array `quantizer_channels[4]`,
// so the facade instance is that array base and the lambdas index
// [idx / per_chan].
//
// Settings model (per-channel subset, MASK excluded): each channel's
// CHANNEL_SETTING_MASK (index 2) is STORAGE_TYPE_U16 (range 1..65535), which
// overflows the int16 _NT_parameter / _NT_algorithm::v (max 32767) and is edited
// through the scale-editor customUI, so it is excluded (the DQ/PASSENCORE U16
// lesson; here a single mask per channel, not DQ's four). The I16 "Fine"
// (-999..999) fits int16 and stays exposed. The other 50 settings/channel ARE
// parameters. Because the excluded mask is mid-array, the facade remaps a
// within-channel logical row w to physical `w < 2 ? w : w + 1`. 4 channels * 50 =
// 200 flat NT rows. The masks still persist: the four channels live in a separate
// file-scope array (not one SettingsBase), so the facade blob hooks are overridden
// to the whole-app QQ_save/QQ_restore, which serialise all four channels' full 51
// settings each (masks included).

#define NT_OC_APP_TU 1

// Pull the vendor UI event type FIRST (before the runtime includes OC_apps.h):
// QQ instantiates a vendor UI editor (OC::ScaleEditor<QuantizerChannel>, a member
// of qq_state) whose template body does member access on UI::Event inside
// namespace OC. Defining UI_EVENTS_H_ up front makes the shim OC_apps.h alias
// OC::UI to ::UI so the editor compiles against the complete event type (see
// OC_apps.h and the PASSENCORE/DQ precedent).
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
#include "OC_visualfx.h"          // OC::vfx::ScrollingHistory (header-only)
#include "OC_scales.h"
#include "OC_scale_edit.h"
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
#include "util/util_turing.h"     // util::TuringShiftRegister (header-only)
#include "util/util_math.h"       // SlewedValue / USAT16 (shim shadow; ARM does
                                  // not force-include it, so pull it explicitly)
#include "util/util_logistic_map.h"  // util::LogisticMap (vendor header-only, pure
                                     // integer math; no shim shadow needed)

#include "../../shim/include/oc_app_manifests/QQ.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

// APP_QQ.h calls multiply_u32xu32_rshift32 (logistic-map / Turing scaling) but,
// unlike ASR/DQ, does not include vendor extern/dspinst.h (which defines it). The
// shim util/util_math.h deliberately omits this variant to avoid colliding with
// dspinst.h in those apps, so QQ supplies it locally. 64-bit-intermediate
// equivalent of dspinst.h's ARM umull; identical numeric result on both targets.
static inline uint32_t multiply_u32xu32_rshift32(uint32_t a, uint32_t b) {
    return static_cast<uint32_t>((static_cast<uint64_t>(a) * b) >> 32);
}

namespace menu = OC::menu;

// Vendor extern menu-redraw flag (on hardware OC_ui.cpp owns it). Same as the
// other ports; the per-app runtime redraws unconditionally.
uint_fast8_t MENU_REDRAW = 1;

#define ENABLE_APP_QUANTERMAIN 1   // APP_QQ.h is guarded by ENABLE_APP_QUANTERMAIN
#include "APP_QQ.h"

namespace {

using ManifestNS = oc_app::QQ;

struct QQInstance : public oc_runtime::AppAlgorithm {};
QQInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_qq_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              QQ_init,
    /* storageSize */       QQ_storageSize,
    /* Save */              QQ_save,
    /* Restore */           QQ_restore,
    /* HandleAppEvent */    QQ_handleAppEvent,
    /* loop */              QQ_loop,
    /* DrawMenu */          QQ_menu,
    /* DrawScreensaver */   QQ_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(QQ_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(QQ_handleEncoderEvent),
    /* isr */               QQ_isr,
};

constexpr int kNumChannels       = 4;
constexpr int kMaskIndex         = CHANNEL_SETTING_MASK;             // 2
constexpr int kExposedPerChannel = CHANNEL_SETTING_LAST - 1;         // 50
constexpr int kNumSettings       = kNumChannels * kExposedPerChannel;  // 200

// Within-channel logical row -> physical setting, skipping the single U16 mask at
// kMaskIndex.
constexpr int phys_in_channel(int w) {
    return w < kMaskIndex ? w : w + 1;
}

// Channel-prefixed parameter names ("1 Scale" .. "4 ..."), filled once at
// construct. The NT parameter .name pointer must outlive construct; this
// file-scope static satisfies it.
char g_names[kNumSettings][16];

void build_names() {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int w = 0; w < kExposedPerChannel; ++w) {
            const int i = ch * kExposedPerChannel + w;
            const char* vn =
                QuantizerChannel::value_attr(static_cast<size_t>(phys_in_channel(w))).name;
            char* dst = g_names[i];
            dst[0] = static_cast<char>('1' + ch);
            dst[1] = ' ';
            size_t len = std::strlen(vn);
            if (len > 13) len = 13;  // 16 - 2 prefix - 1 null
            std::memcpy(dst + 2, vn, len);
            dst[2 + len] = '\0';
        }
    }
}

// The quad facade. instance is the vendor `quantizer_channels` array base; the
// lambdas are captureless (they reference the file-scope array and the QQ_*
// whole-app persistence thunks).
oc_runtime::SettingsFacade make_quad_facade() {
    oc_runtime::SettingsFacade f;
    f.instance = quantizer_channels;
    f.num_settings = kNumSettings;  // overwritten by construct_with_facade
    f.get_value = [](void* self, int idx) -> int {
        const int phys = phys_in_channel(idx % kExposedPerChannel);
        return static_cast<QuantizerChannel*>(self)[idx / kExposedPerChannel]
            .get_value(static_cast<size_t>(phys));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        const int phys = phys_in_channel(idx % kExposedPerChannel);
        return static_cast<QuantizerChannel*>(self)[idx / kExposedPerChannel]
            .apply_value(static_cast<size_t>(phys), value);
    };
    f.save = [](void* /*self*/, void* blob) -> size_t { return QQ_save(blob); };
    f.restore = [](void* /*self*/, const void* blob) -> size_t { return QQ_restore(blob); };
    f.storage_size = []() -> size_t { return QQ_storageSize(); };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &QuantizerChannel::value_attr(
            static_cast<size_t>(phys_in_channel(idx % kExposedPerChannel)));
    };
    f.param_name = [](void* /*self*/, int idx) -> const char* { return g_names[idx]; };
    return f;
}

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // 12 I/O routing rows + 200 exposed settings (50 per channel; the U16 mask per
    // channel is NOT a parameter).
    req.numParameters = oc_runtime::kIoParamCount + kNumSettings;  // 212
    req.sram = sizeof(QQInstance);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) QQInstance();
    g_instance = inst;
    build_names();
    oc_runtime::construct_with_facade(*inst, &the_qq_app, make_quad_facade(),
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
    // The vendor handleButtonEvent / handleEncoderEvent read
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
// Test seams. The vendor QuantizerChannel type and the `quantizer_channels`
// array are only visible in this TU. The setting seams take a (channel,
// physical-setting) pair so a test can address any setting, including the
// excluded mask, directly.
// ---------------------------------------------------------------------------

int qq_get_setting(int channel, int setting) {
    return quantizer_channels[channel].get_value(static_cast<size_t>(setting));
}
bool qq_apply_setting(int channel, int setting, int value) {
    return quantizer_channels[channel].apply_value(static_cast<size_t>(setting), value);
}
int qq_num_channels() { return kNumChannels; }                          // 4
int qq_settings_per_channel_total() { return CHANNEL_SETTING_LAST; }    // 51
int qq_exposed_per_channel() { return kExposedPerChannel; }             // 50
int qq_setting_count() { return kNumSettings; }                         // 200
int qq_settings_param_base() { return oc_runtime::settings_param_base(); }
// Within-channel logical row -> physical setting, for the subset-mapping test.
int qq_phys_in_channel(int within) { return phys_in_channel(within); }
// The excluded U16 mask, addressed directly.
int qq_get_mask(int channel) {
    return quantizer_channels[channel].get_value(CHANNEL_SETTING_MASK);
}
void qq_set_mask(int channel, int value) {
    quantizer_channels[channel].apply_value(CHANNEL_SETTING_MASK, value);
}
// The four DAC output codes (channel i -> DAC i).
void qq_get_outputs(int out[4]) {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<int>(OC::DAC::value(static_cast<size_t>(i)));
}
void qq_arm_sentinel(_NT_algorithm* self) {
    static_cast<QQInstance*>(self)->alive = true;
}
