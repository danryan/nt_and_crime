#pragma once
#include <cstdint>
#include "OC_DAC.h"

namespace HS {

struct OutputCell {
    int value = 0;
    int target = 0;
    int get_target() const { return target; }
    void set(int v) { value = target = v; }
};

struct IOFrame {
    int  inputs[4]            = { 0 };  // 4 inputs (T4.1)
    OutputCell outputs[4];               // 4 outputs (T4.1)
    bool clocked[4]           = { false };
    bool gate_high[4]         = { false };
    bool changed_cv[4]        = { false };
    uint32_t cycle_ticks[4]   = { 0 };
    int  adc_lag_countdown[4] = { -1 };
    uint32_t tick             = 0;

    void Out(DAC_CHANNEL ch, int value) { outputs[ch].set(value); }
    int  ViewOut(int ch) const { return outputs[ch].value; }
    void ClockOut(DAC_CHANNEL ch, int ticks);  // defined in globals.cpp
};

extern IOFrame frame;

}  // namespace HS

using namespace HS;
