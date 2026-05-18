#pragma once
#include <cstdint>

enum DAC_CHANNEL {
    DAC_CHANNEL_A,
    DAC_CHANNEL_B,
    DAC_CHANNEL_C,
    DAC_CHANNEL_D,
    DAC_CHANNEL_LAST
};

namespace OC {
namespace DAC {
// Octave bias for MIDIQuantizer note-number conversion. Vendor
// OC_DAC.h defines this on the target hardware as 5; tests and
// vendor MIDIQuantizer pass it as the default `bias` parameter.
static constexpr uint8_t kOctaveZero = 5;
}
}
