// Dual Quantizer: O_C APP_DQ port (vendor "Meta-Q"). Two independent quantizer
// channels (DQ_QuantizerChannel, each a SettingsBase of 31 settings), each with
// four scale slots, a Turing-machine source, and a main + aux CV output. Channel
// 0 drives DAC A (main) + C (aux), channel 1 drives DAC B (main) + D (aux); both
// quantize a routed CV input to 1V/oct pitch. The quantizer DSP, the scale/mask
// editor, the menu and screensaver draw code, and the ISR compile unmodified.
//
// Multi-channel facade port (the BBGEN quad-facade shape with N = 2). The vendor
// keeps the two channels in the file-scope array `dq_quantizer_channels[2]`, so
// the facade instance is that array base and the lambdas index [idx / per_chan].
//
// Settings model (per-channel subset, masks excluded): each channel has four
// STORAGE_TYPE_U16 scale masks (DQ_CHANNEL_SETTING_MASK1..MASK4, range 1..65535,
// contiguous indices 9..12). _NT_parameter / _NT_algorithm::v are int16_t (max
// 32767), so the masks overflow and cannot be parameters (the FPART/PASSENCORE
// U16 lesson); they are edited through the scale-editor customUI. The other 27
// settings/channel ARE parameters. Because the excluded block is mid-array, the
// facade remaps a within-channel logical row w to physical `w < 9 ? w : w + 4`.
// 2 channels * 27 = 54 flat NT rows. The masks still persist: DQ_save serialises
// both channels' full SettingsBase (all 33 each, masks included), and the facade
// blob hooks are overridden to the whole-app DQ_save/DQ_restore.

#define NT_OC_APP_TU 1

// Pull the vendor UI event type FIRST (before the runtime includes OC_apps.h):
// DQ instantiates a vendor UI editor (OC::ScaleEditor<DQ_QuantizerChannel>, a
// member of dq_state) whose template body does member access on UI::Event inside
// namespace OC. Defining UI_EVENTS_H_ up front makes the shim OC_apps.h alias
// OC::UI to ::UI so the editor compiles against the complete event type (see
// OC_apps.h and the PASSENCORE precedent).
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
#include "util/util_turing.h"     // util::TuringShiftRegister (header-only; the
                                  // vendor app uses it without including it)

#include "../../shim/include/oc_app_manifests/DQ.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

namespace menu = OC::menu;

// The vendor app writes `extern uint_fast8_t MENU_REDRAW;` to flag the firmware
// menu to repaint (on hardware OC_ui.cpp owns the definition; the shim does not
// compile that TU). The per-app runtime redraws unconditionally, so the value is
// otherwise unobserved. Same as Harrington1200.cpp.
uint_fast8_t MENU_REDRAW = 1;

#define ENABLE_APP_METAQ 1        // APP_DQ.h is guarded by ENABLE_APP_METAQ
#include "APP_DQ.h"

namespace {

using ManifestNS = oc_app::DQ;

struct DQInstance : public oc_runtime::AppAlgorithm {};
DQInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_dq_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              DQ_init,
    /* storageSize */       DQ_storageSize,
    /* Save */              DQ_save,
    /* Restore */           DQ_restore,
    /* HandleAppEvent */    DQ_handleAppEvent,
    /* loop */              DQ_loop,
    /* DrawMenu */          DQ_menu,
    /* DrawScreensaver */   DQ_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(DQ_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(DQ_handleEncoderEvent),
    /* isr */               DQ_isr,
};

constexpr int kNumChannels       = NUMCHANNELS;                       // 2
constexpr int kMaskFirst         = DQ_CHANNEL_SETTING_MASK1;          // 9
constexpr int kNumMasks          = 4;                                 // MASK1..MASK4
constexpr int kExposedPerChannel = DQ_CHANNEL_SETTING_LAST - kNumMasks;  // 27
constexpr int kNumSettings       = kNumChannels * kExposedPerChannel;   // 54

// Within-channel logical row -> physical setting, skipping the four contiguous
// U16 masks at [kMaskFirst, kMaskFirst + kNumMasks).
constexpr int phys_in_channel(int w) {
    return w < kMaskFirst ? w : w + kNumMasks;
}

// Channel-prefixed parameter names ("1 scale" .. "2 > LFSR TRIG"), filled once
// at construct. The NT parameter .name pointer must outlive construct; this
// file-scope static satisfies it.
char g_names[kNumSettings][16];

void build_names() {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int w = 0; w < kExposedPerChannel; ++w) {
            const int i = ch * kExposedPerChannel + w;
            const char* vn =
                DQ_QuantizerChannel::value_attr(static_cast<size_t>(phys_in_channel(w))).name;
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

// The dual facade. instance is the vendor `dq_quantizer_channels` array base;
// the lambdas are captureless (they reference the file-scope array and the DQ_*
// whole-app persistence thunks).
oc_runtime::SettingsFacade make_dual_facade() {
    oc_runtime::SettingsFacade f;
    f.instance = dq_quantizer_channels;
    f.num_settings = kNumSettings;  // overwritten by construct_with_facade
    f.get_value = [](void* self, int idx) -> int {
        const int phys = phys_in_channel(idx % kExposedPerChannel);
        return static_cast<DQ_QuantizerChannel*>(self)[idx / kExposedPerChannel]
            .get_value(static_cast<size_t>(phys));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        const int phys = phys_in_channel(idx % kExposedPerChannel);
        return static_cast<DQ_QuantizerChannel*>(self)[idx / kExposedPerChannel]
            .apply_value(static_cast<size_t>(phys), value);
    };
    f.save = [](void* /*self*/, void* blob) -> size_t { return DQ_save(blob); };
    f.restore = [](void* /*self*/, const void* blob) -> size_t { return DQ_restore(blob); };
    f.storage_size = []() -> size_t { return DQ_storageSize(); };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &DQ_QuantizerChannel::value_attr(
            static_cast<size_t>(phys_in_channel(idx % kExposedPerChannel)));
    };
    f.param_name = [](void* /*self*/, int idx) -> const char* { return g_names[idx]; };
    return f;
}

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // 12 I/O routing rows + 54 exposed settings (27 per channel; the four U16
    // masks per channel are NOT parameters).
    req.numParameters = oc_runtime::kIoParamCount + kNumSettings;  // 66
    req.sram = sizeof(DQInstance);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) DQInstance();
    g_instance = inst;
    build_names();
    oc_runtime::construct_with_facade(*inst, &the_dq_app, make_dual_facade(),
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
    // The vendor handleButtonEvent reads EVENT_BUTTON_LONG_PRESS (long-press on
    // BUTTON_DOWN toggles a channel; on the menu it opens the scale editor), so
    // the runtime LONG_RELEASE maps to the vendor LONG_PRESS.
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
// Test seams. The vendor DQ_QuantizerChannel type and the
// `dq_quantizer_channels` array are only visible in this TU. The setting seams
// take a (channel, physical-setting) pair so a test can address any setting,
// including the excluded masks, directly.
// ---------------------------------------------------------------------------

int dq_get_setting(int channel, int setting) {
    return dq_quantizer_channels[channel].get_value(static_cast<size_t>(setting));
}
bool dq_apply_setting(int channel, int setting, int value) {
    return dq_quantizer_channels[channel].apply_value(static_cast<size_t>(setting), value);
}
int dq_num_channels() { return kNumChannels; }
int dq_settings_per_channel_total() { return DQ_CHANNEL_SETTING_LAST; }   // 33
int dq_exposed_per_channel() { return kExposedPerChannel; }               // 27
int dq_setting_count() { return kNumSettings; }                           // 54
int dq_settings_param_base() { return oc_runtime::settings_param_base(); }
// Within-channel logical row -> physical setting, for the subset-mapping test.
int dq_phys_in_channel(int within) { return phys_in_channel(within); }
// A mask slot (0..3), the excluded U16 setting, addressed directly.
int dq_get_mask(int channel, int slot) {
    return dq_quantizer_channels[channel].get_value(
        static_cast<size_t>(DQ_CHANNEL_SETTING_MASK1 + slot));
}
void dq_set_mask(int channel, int slot, int value) {
    dq_quantizer_channels[channel].apply_value(
        static_cast<size_t>(DQ_CHANNEL_SETTING_MASK1 + slot), value);
}
// The four DAC output codes (ch0 -> A,C; ch1 -> B,D).
void dq_get_outputs(int out[4]) {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<int>(OC::DAC::value(static_cast<size_t>(i)));
}
void dq_arm_sentinel(_NT_algorithm* self) {
    static_cast<DQInstance*>(self)->alive = true;
}
