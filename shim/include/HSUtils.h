#pragma once
#include "OC_gpio.h"

#define ONE_OCTAVE (12 << 7)                                    // 1536 hem units per V
#define HEMISPHERE_MAX_INPUT_CV (6 * ONE_OCTAVE)                // 9216 (T4.1)
#define HEMISPHERE_MAX_CV (6 * ONE_OCTAVE)                      // PULSE_VOLTAGE * ONE_OCTAVE
#define HEMISPHERE_CENTER_INPUT_CV 0                            // (NorthernLightModular = 0)
#define HEMISPHERE_CENTER_DETENT 80
#define HEMISPHERE_CLOCK_TICKS 17
#define HEMISPHERE_CURSOR_TICKS 5000

#define PULSE_VOLTAGE 6                                         // octave_max on T4.1

#define ForEachChannel(ch) for (int_fast8_t ch = 0; (ch) < 2; ++(ch))
#define gfx_offset 0                                            // shim renders single applet at left
#define io_offset 0                                             // shim's frame indexes from 0

namespace HS {
enum HEM_SIDE : uint8_t { LEFT_HEMISPHERE = 0, APPLET_CURSOR_COUNT };
enum HELP_SECTIONS {
    HELP_DIGITAL1 = 0, HELP_DIGITAL2,
    HELP_CV1, HELP_CV2,
    HELP_OUT1, HELP_OUT2,
    HELP_EXTRA1, HELP_EXTRA2,
    HELP_LABEL_COUNT
};
}
using namespace HS;

constexpr void Pack(uint64_t& data, struct PackLocation p, uint64_t value);
constexpr int Unpack(const uint64_t& data, struct PackLocation p);

struct PackLocation { size_t location; size_t size; };
constexpr void Pack(uint64_t& data, PackLocation p, uint64_t value) {
    data |= (value << p.location);
}
constexpr int Unpack(const uint64_t& data, PackLocation p) {
    uint64_t mask = 1;
    for (size_t i = 1; i < p.size; ++i) mask |= (uint64_t(1) << i);
    return (data >> p.location) & mask;
}

// Help array — populated by applet's SetHelp, read by debug/help screens.
extern const char* help_strings[HS::HELP_LABEL_COUNT];

// Cursor blink countdown — used by HemisphereApplet::CursorBlink().
extern int cursor_countdown[HS::APPLET_CURSOR_COUNT];

// EditMode toggle state per side. Logic uses just LEFT_HEMISPHERE.
struct EncoderEditor { bool isEditing; };
extern EncoderEditor enc_edit[HS::APPLET_CURSOR_COUNT];
