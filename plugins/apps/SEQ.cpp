// Sequins: O_C APP_SEQ port (vendor "Sequins"). A dual-channel step sequencer.
// Each channel walks a user pattern (forward / reverse / pendulum / random /
// brownian / arpeggiated), quantizes the stepped note to a scale + 12-bit
// active-note mask, and emits 1V/oct pitch on its main DAC channel plus a peaks
// multistage envelope on its aux DAC channel. Channel 0 drives DAC A (main) + C
// (aux), channel 1 drives DAC B (main) + D (aux). The sequencer engine, the arp,
// the pattern + scale editors, the menu / screensaver draw code, and the ISR all
// compile unmodified from the vendor header.
//
// Multi-channel facade port (the DQ shape: the BBGEN quad facade with N = 2). The
// vendor keeps the two channels in the file-scope array `seq_channel[2]`, so the
// facade instance is that array base and the lambdas index [idx / per_channel].
//
// Settings model (per-channel subset, masks excluded): each channel has five
// STORAGE_TYPE_U16 masks at contiguous indices 10..14 (SEQ_CHANNEL_SETTING_
// SCALE_MASK plus the four sequence masks MASK1..MASK4, range up to 65535).
// _NT_parameter / _NT_algorithm::v are int16_t (max 32767), so these overflow and
// cannot be parameters (the FPART / PASSENCORE / DQ U16 lesson); they are edited
// through the scale-editor and pattern-editor customUI. The other 53 settings per
// channel ARE parameters. Because the excluded block is mid-array, the facade
// remaps a within-channel logical row w to physical `w < 10 ? w : w + 5`. 2
// channels * 53 = 106 flat NT rows. The masks still persist: SEQ_save serialises
// both channels' full SettingsBase (all 58 each, masks included), and the facade
// blob hooks are overridden to the whole-app SEQ_save / SEQ_restore (the DQ / QQ /
// AUTOMATONNETZ separate-array blob-override; seq_channel is a separate array, not
// the facade's own SettingsBase).

#define NT_OC_APP_TU 1

// Pull the vendor UI event type FIRST (before the runtime includes OC_apps.h):
// SEQ instantiates two vendor UI editors (OC::PatternEditor<SEQ_Channel> and
// OC::ScaleEditor<SEQ_Channel>, members of seq_state) whose template bodies do
// member access on UI::Event inside namespace OC. Defining UI_EVENTS_H_ up front
// makes the shim OC_apps.h alias OC::UI to ::UI so the editors compile against the
// complete event type (see OC_apps.h and the DQ / PASSENCORE precedent).
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
#include "OC_patterns.h"          // OC::Patterns, OC::pattern_names_short
#include "OC_sequence_edit.h"     // OC::PatternEditor (header-only)
#include "OC_input_map.h"
#include "OC_input_maps.h"
#include "Arduino.h"
#include "hem_graphics.h"
// SlewedValue lives in the shim util/util_math.h shadow. APP_SEQ.h uses it
// (SlewedValue output_) without transitively including util_math.h first, and the
// ARM build (unlike the host test rule) does not force-include the shadow, so pull
// it explicitly here before the vendor header or the ARM compile fails to name the
// type (the ASR lesson).
#include "util/util_math.h"
#include "util/util_settings.h"
#include "util/util_trigger_delay.h"  // OC::TriggerDelay (header-only)
#include "util/util_arp.h"            // util::Arpeggiator (header-only)
#include "extern/dspinst.h"           // multiply_u32xu32_rshift32 (the SEQ scaler)
#include "peaks_multistage_envelope.h"

#include "../../shim/include/oc_app_manifests/SEQ.h"

#include <distingnt/api.h>
#include <cstring>
#include <new>

namespace menu = OC::menu;

// The vendor app writes `extern uint_fast8_t MENU_REDRAW;` to flag the firmware
// menu to repaint (on hardware OC_ui.cpp owns the definition; the shim does not
// compile that TU). The per-app runtime redraws unconditionally, so the value is
// otherwise unobserved. Same as the other ports.
uint_fast8_t MENU_REDRAW = 1;

#define ENABLE_APP_SEQUINS 1      // APP_SEQ.h is guarded by ENABLE_APP_SEQUINS
#include "APP_SEQ.h"

namespace {

using ManifestNS = oc_app::SEQ;

constexpr int kNumChannels       = NUM_CHANNELS;                          // 2
constexpr int kMaskFirst         = SEQ_CHANNEL_SETTING_SCALE_MASK;        // 10
constexpr int kNumMasks          = 5;  // SCALE_MASK + MASK1..MASK4, contiguous
constexpr int kExposedPerChannel = SEQ_CHANNEL_SETTING_LAST - kNumMasks;  // 53
constexpr int kNumSettings       = kNumChannels * kExposedPerChannel;     // 106

// Channel-prefixed parameter names live INSIDE the per-instance struct (in the
// firmware-allocated ptrs.sram), NOT a file-scope .bss array: the firmware reads
// parameters[].name during add-algorithm, and a pointer into the plugin's global
// .bss hard-faults the firmware on dereference. See DQ.cpp for the full rationale.
struct SeqInstance : public oc_runtime::AppAlgorithm {
    char names[kNumSettings][16];
};
SeqInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

const OC::App the_seq_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              SEQ_init,
    /* storageSize */       SEQ_storageSize,
    /* Save */              SEQ_save,
    /* Restore */           SEQ_restore,
    /* HandleAppEvent */    SEQ_handleAppEvent,
    /* loop */              SEQ_loop,
    /* DrawMenu */          SEQ_menu,
    /* DrawScreensaver */   SEQ_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(SEQ_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(SEQ_handleEncoderEvent),
    /* isr */               SEQ_isr,
};

// Within-channel logical row -> physical setting, skipping the five contiguous
// U16 masks at [kMaskFirst, kMaskFirst + kNumMasks).
constexpr int phys_in_channel(int w) {
    return w < kMaskFirst ? w : w + kNumMasks;
}

// Fill the per-instance name buffer (firmware-allocated SRAM, firmware-readable).
void build_names(SeqInstance* inst) {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int w = 0; w < kExposedPerChannel; ++w) {
            const int i = ch * kExposedPerChannel + w;
            const char* vn =
                SEQ_Channel::value_attr(static_cast<size_t>(phys_in_channel(w))).name;
            char* dst = inst->names[i];
            dst[0] = static_cast<char>('1' + ch);
            dst[1] = ' ';
            size_t len = std::strlen(vn);
            if (len > 13) len = 13;  // 16 - 2 prefix - 1 null
            std::memcpy(dst + 2, vn, len);
            dst[2 + len] = '\0';
        }
    }
}

// The dual facade. instance is the vendor `seq_channel` array base; the lambdas
// are captureless (they reference the file-scope array and the SEQ_* whole-app
// persistence thunks).
oc_runtime::SettingsFacade make_dual_facade() {
    oc_runtime::SettingsFacade f;
    f.instance = seq_channel;
    f.num_settings = kNumSettings;  // overwritten by construct_with_facade
    f.get_value = [](void* self, int idx) -> int {
        const int phys = phys_in_channel(idx % kExposedPerChannel);
        return static_cast<SEQ_Channel*>(self)[idx / kExposedPerChannel]
            .get_value(static_cast<size_t>(phys));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        const int phys = phys_in_channel(idx % kExposedPerChannel);
        return static_cast<SEQ_Channel*>(self)[idx / kExposedPerChannel]
            .apply_value(static_cast<size_t>(phys), value);
    };
    f.save = [](void* /*self*/, void* blob) -> size_t { return SEQ_save(blob); };
    f.restore = [](void* /*self*/, const void* blob) -> size_t { return SEQ_restore(blob); };
    f.storage_size = []() -> size_t { return SEQ_storageSize(); };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &SEQ_Channel::value_attr(
            static_cast<size_t>(phys_in_channel(idx % kExposedPerChannel)));
    };
    f.param_name = [](void* /*self*/, int idx) -> const char* {
        return g_instance->names[idx];
    };
    return f;
}

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // 12 I/O routing rows + 106 exposed settings (53 per channel; the five U16
    // masks per channel are NOT parameters).
    req.numParameters = oc_runtime::kIoParamCount + kNumSettings;  // 118
    req.sram = sizeof(SeqInstance);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) SeqInstance();
    g_instance = inst;
    build_names(inst);
    oc_runtime::construct_with_facade(*inst, &the_seq_app, make_dual_facade(),
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
    // The vendor handleButtonEvent and both editors (pattern + scale) read
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
// Test seams. The vendor SEQ_Channel type and the `seq_channel` array are only
// visible in this TU. The setting seams take a (channel, physical-setting) pair so
// a test can address any setting, including the excluded masks, directly.
// ---------------------------------------------------------------------------

int sq_get_setting(int channel, int setting) {
    return seq_channel[channel].get_value(static_cast<size_t>(setting));
}
bool sq_apply_setting(int channel, int setting, int value) {
    return seq_channel[channel].apply_value(static_cast<size_t>(setting), value);
}
int sq_num_channels() { return kNumChannels; }
int sq_settings_per_channel_total() { return SEQ_CHANNEL_SETTING_LAST; }  // 58
int sq_exposed_per_channel() { return kExposedPerChannel; }              // 53
int sq_setting_count() { return kNumSettings; }                          // 106
int sq_settings_param_base() { return oc_runtime::settings_param_base(); }
// Within-channel logical row -> physical setting, for the subset-mapping test.
int sq_phys_in_channel(int within) { return phys_in_channel(within); }
int sq_mask_first() { return kMaskFirst; }                               // 10
int sq_num_masks() { return kNumMasks; }                                 // 5
// A mask slot (0..4), the excluded U16 setting, addressed directly.
int sq_get_mask(int channel, int slot) {
    return seq_channel[channel].get_value(
        static_cast<size_t>(SEQ_CHANNEL_SETTING_SCALE_MASK + slot));
}
void sq_set_mask(int channel, int slot, int value) {
    seq_channel[channel].apply_value(
        static_cast<size_t>(SEQ_CHANNEL_SETTING_SCALE_MASK + slot), value);
}
// The four DAC output codes (ch0 -> A,C; ch1 -> B,D).
void sq_get_outputs(int out[4]) {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<int>(OC::DAC::value(static_cast<size_t>(i)));
}
void sq_arm_sentinel(_NT_algorithm* self) {
    static_cast<SeqInstance*>(self)->alive = true;
}
