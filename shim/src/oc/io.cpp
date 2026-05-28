// O_C-only I/O backing: the ADC_CHANNEL_* extern channel objects, the O_C
// input store, and the digital-input trigger store with rising-edge detection.
//
// This translation unit is O_C-only. It is NOT part of SHIM_CORE_SRCS and is
// NOT included by hem_shim_impl.h, so it never aggregates into a Hemisphere
// applet. The O_C runtime (added in a later task) will include it via the O_C
// aggregation header; for now the host test links it directly.

#include "OC_ADC.h"
#include "OC_digital_inputs.h"

// ADC channel objects. Vendor OC_ADC.h:16 declares these as extern lvalue
// objects so `template <ADC_CHANNEL &channel>` binds to them. Values mirror the
// vendor channel ordinals (1->0, 2->1, ...) so they index the input store.
ADC_CHANNEL ADC_CHANNEL_1 = 0;
ADC_CHANNEL ADC_CHANNEL_2 = 1;
ADC_CHANNEL ADC_CHANNEL_3 = 2;
ADC_CHANNEL ADC_CHANNEL_4 = 3;

namespace oc_io {
namespace {

int input_[ADC_CHANNEL_COUNT] = { 0 };

// Live trigger levels and the latched rising-edge mask. last_high_ records the
// level at the previous Scan() so the next Scan() can detect a low->high edge.
bool trigger_high_[OC::DIGITAL_INPUT_LAST] = { false };
bool last_high_[OC::DIGITAL_INPUT_LAST] = { false };
uint32_t clocked_mask_ = 0;

}  // namespace

void set_input(int channel, int value) {
    input_[channel] = value;
}

int input(int channel) {
    return input_[channel];
}

void set_trigger(int input, bool high) {
    trigger_high_[input] = high;
}

bool trigger(int input) {
    return trigger_high_[input];
}

// Latch low->high transitions since the previous Scan() into clocked_mask_.
// One bus edge held across consecutive Scan() calls latches exactly once: the
// flag sets on the rising Scan and clears on the next, matching the Hemisphere
// rising-edge discipline (one edge seen by one tick).
void scan_triggers() {
    uint32_t mask = 0;
    for (int i = 0; i < OC::DIGITAL_INPUT_LAST; ++i) {
        if (trigger_high_[i] && !last_high_[i]) {
            mask |= (0x1u << i);
        }
        last_high_[i] = trigger_high_[i];
    }
    clocked_mask_ = mask;
}

uint32_t clocked_mask() {
    return clocked_mask_;
}

}  // namespace oc_io
