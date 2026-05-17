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

#include "HSIOFrame.h"
HS::IOFrame HS::frame;

void HS::IOFrame::ClockOut(DAC_CHANNEL ch, int) {
    // Simplest: drive the output high for one Controller tick by setting value high.
    // The host's bus write picks up the value next time.
    outputs[ch].set(PULSE_VOLTAGE * ONE_OCTAVE);
}

#include "CVInputMap.h"
CVInputMap cvmap[4] = {{0}, {1}, {2}, {3}};
DigitalInputMap trigmap[4] = {{0}, {1}, {2}, {3}};
