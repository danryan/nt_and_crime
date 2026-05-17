#include "HSUtils.h"
#include "OC_core.h"
#include "OC_strings.h"
#include "HSClockManager.h"

namespace OC {
namespace CORE {
volatile uint32_t ticks = 0;
}
}

namespace HS {
const char* help_strings[HS::HELP_LABEL_COUNT] = { nullptr };
int cursor_countdown[HS::APPLET_CURSOR_COUNT] = { 0 };
EncoderEditor enc_edit[HS::APPLET_CURSOR_COUNT] = {{ false }};
}

namespace OC {
namespace Strings {
const char* const capital_letters[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
}
}

HSClockManager clock_m;

uint32_t hem_rng_state = 0x12345678u;

#include "HSIOFrame.h"
HS::IOFrame HS::frame;

void HS::IOFrame::ClockOut(DAC_CHANNEL ch, int pulselength) {
    if (pulselength <= 0) pulselength = HEMISPHERE_CLOCK_TICKS;
    outputs[ch].set(PULSE_VOLTAGE * ONE_OCTAVE);
    clock_countdown[ch] = pulselength;
}

#include "CVInputMap.h"
CVInputMap cvmap[4] = {CVInputMap(0), CVInputMap(1), CVInputMap(2), CVInputMap(3)};
DigitalInputMap trigmap[4] = {DigitalInputMap(0), DigitalInputMap(1), DigitalInputMap(2), DigitalInputMap(3)};
