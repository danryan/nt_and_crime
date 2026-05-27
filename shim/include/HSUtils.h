#pragma once
#include <cstdint>
#include <cstddef>
#include "OC_gpio.h"

// Vendor braids_quantizer.h (pulled in via OC_scales.h) contains a
// tautological NULL check on a stack-allocated array. The compiler warning is
// correct but the vendor source is upstream-frozen. Suppress at the include
// site so make arm stays warning-clean.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#include "OC_scales.h"
#pragma GCC diagnostic pop

#define ONE_OCTAVE (12 << 7)                                    // 1536 hem units per V
#define HEMISPHERE_MAX_INPUT_CV (6 * ONE_OCTAVE)                // 9216 (T4.1)
#define HEMISPHERE_MAX_CV (6 * ONE_OCTAVE)                      // PULSE_VOLTAGE * ONE_OCTAVE
#define HEMISPHERE_CENTER_INPUT_CV 0                            // (NorthernLightModular = 0)
#define HEMISPHERE_CENTER_DETENT 80
#define HEMISPHERE_CLOCK_TICKS 175
#define HEMISPHERE_CURSOR_TICKS 5000
#define HEMISPHERE_3V_CV (3 * ONE_OCTAVE)
static constexpr uint32_t HEMISPHERE_PULSE_ANIMATION_TIME = 500;
static constexpr uint32_t HEMISPHERE_PULSE_ANIMATION_TIME_LONG = 1200;
namespace HS {
static constexpr uint32_t HEMISPHERE_DOUBLE_CLICK_TIME = 8000;
}

#define PULSE_VOLTAGE 6                                         // octave_max on T4.1
#define HEMISPHERE_MIN_CV (-(PULSE_VOLTAGE * ONE_OCTAVE))
#define HEMISPHERE_ADC_LAG 96

#ifndef int2simfloat
#define int2simfloat(x) ((int32_t)(x) << 14)
#define simfloat2int(x) ((int32_t)(x) >> 14)
using simfloat = int32_t;
#endif

#define ForEachChannel(ch) for (int_fast8_t ch = 0; (ch) < 2; ++(ch))
#define ForAllChannels(ch) for (int_fast8_t ch = 0; (ch) < 4; ++(ch))
namespace HS {
extern int gfx_offset;
extern int gfx_offset_y;
// Clip-rect width and height (screen-space). Hosts set per-frame to bound
// drawing to a single lane's column; defaults are full-screen (256x64) so
// standalone runs and host tests see no clamp. Q1 fix for the Quadrants
// right-edge bleed: vendor applets draw past x=63 in some headers/widgets,
// and without a clip rect that bleed lands in the neighboring lane's
// leftmost column.
extern int gfx_clip_w;
extern int gfx_clip_h;
}
using HS::gfx_offset;
using HS::gfx_offset_y;
using HS::gfx_clip_w;
using HS::gfx_clip_h;
#define BottomAlign(h) (62 - (h))
#define io_offset 0                                             // shim's frame indexes from 0

namespace HS {
enum HEM_SIDE : uint8_t { LEFT_HEMISPHERE = 0, RIGHT_HEMISPHERE, APPLET_CURSOR_COUNT };
enum HELP_SECTIONS {
    HELP_DIGITAL1 = 0, HELP_DIGITAL2,
    HELP_CV1, HELP_CV2,
    HELP_OUT1, HELP_OUT2,
    HELP_EXTRA1, HELP_EXTRA2,
    HELP_LABEL_COUNT
};
}
using namespace HS;

// Mirrors upstream HSUtils::pad. Returns horizontal pixel padding so that
// right-justified integers line up under a given range (each digit = 6 px).
inline uint8_t pad(int range, int number) {
    uint8_t padding = 0;
    while (range > 1) {
        int abs_n = number < 0 ? -number : number;
        if (abs_n < range) padding += 6;
        range = range / 10;
    }
    if (number < 0 && padding > 0) padding -= 6;
    return padding;
}

struct PackLocation { size_t location; size_t size; };
inline void Pack(uint64_t& data, PackLocation p, uint64_t value) {
    data |= (value << p.location);
}
inline int Unpack(const uint64_t& data, PackLocation p) {
    uint64_t mask = 1;
    for (size_t i = 1; i < p.size; ++i) mask |= (uint64_t(1) << i);
    return static_cast<int>((data >> p.location) & mask);
}

namespace HS {

// Popup type / error type enums. Mirrors vendor HSUtils.h. Shim PokePopup
// is a no-op for host tests; the popup UI is not exercised.
enum PopupType : uint8_t {
    MENU_POPUP = 0,
    CLOCK_POPUP,
    PRESET_POPUP,
    QUANTIZER_POPUP,
    MIDI_POPUP,
    AUX_POPUP,
};
enum ErrMsgIndex : uint8_t {
    NO_ERROR = 0,
    MAX_PRESET_ERROR,
};

// Popup-state globals. Mirrors vendor HSUtils.h:189-195.
extern uint8_t qview;        // which quantizer's setting is shown in popup
extern uint8_t mview;        // which midi channel's setting is shown in popup
extern int q_edit;
extern int midi_edit;
extern PopupType popup_type;
extern ErrMsgIndex msg_idx;
extern uint32_t popup_tick;

// PokePopup stubs. Host tests do not exercise popup display; the shim
// definitions in shim/src/globals.cpp are no-ops.
void PokePopup(PopupType pop, ErrMsgIndex err = NO_ERROR);
void PokePopup(PopupType pop, const char* msg);

}  // namespace HS

// IndexedInput free template. Mirrors vendor HSUtils.h:266. Wraps an input
// map reference with a positional index for variadic CheckEditInputMapPress
// dispatch. Defined in global namespace per vendor convention (NOT inside
// HS::); Combin8 calls it as `IndexedInput(CH1_AUX1, ...)`.
#include <utility>
template <typename T>
constexpr std::pair<int, T&&> IndexedInput(int index, T&& input_map) {
    return std::pair<int, T&&>(index, std::forward<T>(input_map));
}

// Channel/auxiliary IDs used by Combin8's IndexedInput calls. Mirrors
// vendor enums.
enum HemisphereInputAux : int {
    CH1_AUX1 = 0,
    CH1_AUX2,
    CH2_AUX1,
    CH2_AUX2,
};

namespace HS {

// Help array — populated by applet's SetHelp, read by debug/help screens.
extern const char* help_strings[HS::HELP_LABEL_COUNT];

// Cursor blink countdown — used by HemisphereApplet::CursorBlink().
extern int cursor_countdown[HS::APPLET_CURSOR_COUNT];

// EditMode toggle state per side. Logic uses just LEFT_HEMISPHERE.
struct EncoderEditor { bool isEditing; };
extern EncoderEditor enc_edit[HS::APPLET_CURSOR_COUNT];

// Quantizer channel pool. Mirrors vendor HSUtils.h:97-107.
enum QUANT_CHANNEL {
    QUANT_CHANNEL_1,
    QUANT_CHANNEL_2,
    QUANT_CHANNEL_3,
    QUANT_CHANNEL_4,
    QUANT_CHANNEL_5,
    QUANT_CHANNEL_6,
    QUANT_CHANNEL_7,
    QUANT_CHANNEL_8,

    QUANT_CHANNEL_COUNT
};

// Mirrors vendor HSUtils.h:122-127.
struct QuantEngineSettings {
    int16_t scale;
    int8_t root_note;
    int8_t octave;
    uint16_t mask;
};

// Mirrors vendor HSUtils.h:128-184. Wraps braids::Quantizer with OC scale
// selection, root note, and octave offset.
struct QuantEngine : public QuantEngineSettings {
    braids::Quantizer quantizer;

    QuantEngine() {
        scale = OC::Scales::SCALE_SEMI;
        mask = 0xffff;
        root_note = 0;
        octave = 0;
        quantizer.Init();
        Reconfig();
    }

    void Reconfig() {
        quantizer.Configure(OC::Scales::GetScale(scale), mask);
    }
    void Configure(int scale_, uint16_t mask_) {
        CONSTRAIN(scale_, 0, OC::Scales::NUM_SCALES - 1);
        scale = scale_;
        if (mask_) mask = mask_;
        Reconfig();
    }
    void EditMask(int idx, bool on) {
        mask = on ? (mask | (1u << idx)) : (mask & ~(1u << idx));
    }
    void NudgeScale(int dir) {
        const int max = OC::Scales::NUM_SCALES;
        scale += dir;
        if (scale >= max) scale = 0;
        if (scale < 0) scale = max - 1;
        Reconfig();
    }
    void RotateMask(int dir) {
        const size_t scale_size = OC::Scales::GetScale(scale).num_notes;
        uint16_t used_bits = ~(0xffffU << scale_size);
        mask &= used_bits;

        if (dir < 0) {
            dir = -dir;
            mask = (mask >> dir) | (mask << (scale_size - dir));
        } else {
            mask = (mask << dir) | (mask >> (scale_size - dir));
        }
        mask |= ~used_bits; // fill upper bits

        Reconfig();
    }

    int Process(int cv, int root, int transpose) {
        if (root == 0) root = (root_note << 7);
        return quantizer.Process(cv, root, transpose) + (octave * ONE_OCTAVE);
    }
    int Lookup(int note) {
        return quantizer.Lookup(note) + (root_note << 7) + (octave * ONE_OCTAVE);
    }

    const int Size() {
        return OC::Scales::GetScale(scale).num_notes;
    }
};

// Global quantizer engine pool. Defined in shim/src/quant/q_engine.cpp.
extern QuantEngine q_engine[QUANT_CHANNEL_COUNT];

// Quantizer helper free functions. Mirrors vendor HSUtils.h:224-235.
QuantEngine& GetQuantEngine(int ch);
int GetLatestNoteNumber(int ch);
int Quantize(int ch, int cv, int root = 0, int transpose = 0);
int QuantizerLookup(int ch, int note);
void QuantizerConfigure(int ch, int scale, uint16_t mask = 0xffff);
int GetScale(int ch);
int GetRootNote(int ch);
int SetRootNote(int ch, int root);
void NudgeRootNote(int ch, int dir);
void NudgeOctave(int ch, int dir);
void NudgeScale(int ch, int dir);
void QuantizerEdit(int ch);
// Vendor HS::SetScale (HSUtils.cpp). Sets scale on channel ch's QuantEngine.
void SetScale(int ch, int scale);

}  // namespace HS
