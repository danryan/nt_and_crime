#pragma once
#include <cstdint>
#include <cstddef>
#include "OC_gpio.h"

#define ONE_OCTAVE (12 << 7)                                    // 1536 hem units per V
#define HEMISPHERE_MAX_INPUT_CV (6 * ONE_OCTAVE)                // 9216 (T4.1)
#define HEMISPHERE_MAX_CV (6 * ONE_OCTAVE)                      // PULSE_VOLTAGE * ONE_OCTAVE
#define HEMISPHERE_CENTER_INPUT_CV 0                            // (NorthernLightModular = 0)
#define HEMISPHERE_CENTER_DETENT 80
#define HEMISPHERE_CLOCK_TICKS 175
#define HEMISPHERE_CURSOR_TICKS 5000

#define PULSE_VOLTAGE 6                                         // octave_max on T4.1
#define HEMISPHERE_MIN_CV (-(PULSE_VOLTAGE * ONE_OCTAVE))
#define HEMISPHERE_ADC_LAG 96

#ifndef int2simfloat
#define int2simfloat(x) ((int32_t)(x) << 14)
#define simfloat2int(x) ((int32_t)(x) >> 14)
using simfloat = int32_t;
#endif

#define ForEachChannel(ch) for (int_fast8_t ch = 0; (ch) < 2; ++(ch))
namespace HS { extern int gfx_offset; }
using HS::gfx_offset;
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

// Help array — populated by applet's SetHelp, read by debug/help screens.
extern const char* help_strings[HS::HELP_LABEL_COUNT];

// Cursor blink countdown — used by HemisphereApplet::CursorBlink().
extern int cursor_countdown[HS::APPLET_CURSOR_COUNT];

// EditMode toggle state per side. Logic uses just LEFT_HEMISPHERE.
struct EncoderEditor { bool isEditing; };
extern EncoderEditor enc_edit[HS::APPLET_CURSOR_COUNT];

}  // namespace HS
