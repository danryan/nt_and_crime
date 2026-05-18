#include "applet_test_helpers.h"
#include "nt_runtime.h"
#include <cmath>
#include <cstring>

// hem_rng_state is defined in shim/src/globals.cpp and declared extern in
// shim/include/HemisphereApplet.h. We re-declare it here so the helper TU
// does not need to pull HemisphereApplet.h (which transitively drags in
// vendor applet headers via hemispheres_shim.h).
extern uint32_t hem_rng_state;

namespace hem_test {

int bus_index(HemSide side, int channel, HemAxis axis) {
    // Channels A,B (0,1) are left side; C,D (2,3) are right side.
    int slot = (side == LEFT) ? channel : (channel + 2);
    switch (axis) {
        case GATE_IN: return 1  + slot;  // 1..4
        case CV_IN:   return 5  + slot;  // 5..8
        case OUT:     return 13 + slot;  // 13..16
    }
    return 0;
}

void set_gate(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, GATE_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    slice[frame_offset] = 6.0f;  // above 0.5f gate threshold
}

void hold_gate(float* bus, HemSide side, int channel, int numFramesBy4) {
    int bidx = bus_index(side, channel, GATE_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = 6.0f;
}

void set_cv(float* bus, HemSide side, int channel, float volts, int numFramesBy4) {
    int bidx = bus_index(side, channel, CV_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = volts;
}

bool read_gate_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, OUT);
    int numFrames = numFramesBy4 * 4;
    return bus[(bidx - 1) * numFrames + frame_offset] > 0.5f;
}

float read_cv_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, OUT);
    int numFrames = numFramesBy4 * 4;
    return bus[(bidx - 1) * numFrames + frame_offset];
}

void select_applet(_NT_algorithm* alg, HemSide side, hem_shim::AppletIndex idx) {
    int param = (side == LEFT) ? 0 : 1;  // kHemSelLeft = 0, kHemSelRight = 1
    const_cast<int16_t*>(alg->v)[param] = (int16_t)idx;
}

int step_n_frames(nt::LoadedPlugin* loaded, _NT_algorithm* alg, float* bus, int n_samples) {
    const int framesPerStep = 32;
    int steps = (n_samples + framesPerStep - 1) / framesPerStep;
    for (int i = 0; i < steps; ++i) {
        loaded->factory->step(alg, bus, 8);
    }
    return steps * framesPerStep;
}

int volts_to_int(float v) {
    return (int)std::lroundf(v * 1536.0f);
}

float int_to_volts(int hem_units) {
    return (float)hem_units / 1536.0f;
}

void seed_hem_rng(uint32_t seed) {
    hem_rng_state = seed;
}

uint64_t pack_calculate(int op_left, int op_right) {
    return ((uint64_t)(op_left  & 0xFF))
         | ((uint64_t)(op_right & 0xFF) << 8);
}

uint64_t pack_brancher(int p) {
    return (uint64_t)(p & 0x7F);
}

uint64_t pack_logic(int op_left, int op_right) {
    return ((uint64_t)(op_left  & 0xFF))
         | ((uint64_t)(op_right & 0xFF) << 8);
}

uint64_t pack_slew(int rise, int fall) {
    return ((uint64_t)(rise & 0xFF))
         | ((uint64_t)(fall & 0xFF) << 8);
}

uint64_t pack_burst(int number, int spacing, int div, int jitter, int accel) {
    uint64_t data = 0;
    data |= ((uint64_t)(number   & 0xFF));
    data |= ((uint64_t)(spacing  & 0xFF)) << 8;
    data |= ((uint64_t)((div + 8) & 0xFF)) << 16;
    data |= ((uint64_t)(jitter   & 0xFF)) << 24;
    data |= ((uint64_t)(accel    & 0xFF)) << 32;
    return data;
}

namespace {
constexpr int kAttenOffMaxLevel = 63;  // mirrors ATTENOFF_MAX_LEVEL in vendor AttenuateOffset.h
}

uint64_t pack_atten_off(int offset_left, int offset_right,
                         int level_left, int level_right,
                         bool mix) {
    uint64_t data = 0;
    data |= ((uint64_t)((offset_left  + 256) & 0x1FF));
    data |= ((uint64_t)((offset_right + 256) & 0x1FF)) << 10;
    data |= ((uint64_t)((level_left  + kAttenOffMaxLevel * 2) & 0xFF)) << 19;
    data |= ((uint64_t)((level_right + kAttenOffMaxLevel * 2) & 0xFF)) << 27;
    data |= ((uint64_t)(mix ? 1 : 0)) << 35;
    return data;
}

uint64_t pack_compare(int level) {
    return (uint64_t)(level & 0xFF);
}

uint64_t pack_clock_divider(int div0, int div1, int divmult1_steps, int divmult3_steps) {
    uint64_t data = 0;
    data |= ((uint64_t)((div0          + 32) & 0xFF));
    data |= ((uint64_t)((div1          + 32) & 0xFF)) << 8;
    data |= ((uint64_t)((divmult1_steps + 32) & 0xFF)) << 16;
    data |= ((uint64_t)((divmult3_steps + 32) & 0xFF)) << 24;
    return data;
}

uint64_t pack_clk_to_gate(int width_a, int range_a, int skip_a,
                          int width_b, int range_b, int skip_b) {
    auto pack_side = [](int width, int range, int skip) -> uint64_t {
        uint64_t side = 0;
        side |= ((uint64_t)(width & 0x7F));
        side |= ((uint64_t)((range < 0 ? -range : range) & 0x7F)) << 8;
        side |= ((uint64_t)(range < 0 ? 1 : 0)) << 15;
        side |= ((uint64_t)(skip & 0x7F)) << 16;
        return side;
    };
    uint64_t data = 0;
    data |= pack_side(width_a, range_a, skip_a);
    data |= pack_side(width_b, range_b, skip_b) << 32;
    return data;
}

uint64_t pack_gate_delay(int time_left, int time_right) {
    return ((uint64_t)(time_left  & 0x7FF))
         | ((uint64_t)(time_right & 0x7FF) << 11);
}

uint64_t pack_tlneuron(int w0, int w1, int w2, int threshold) {
    uint64_t data = 0;
    data |= ((uint64_t)((w0 + 9) & 0x1F));
    data |= ((uint64_t)((w1 + 9) & 0x1F)) << 5;
    data |= ((uint64_t)((w2 + 9) & 0x1F)) << 10;
    data |= ((uint64_t)((threshold + 27) & 0x3F)) << 15;
    return data;
}

uint64_t pack_cumulus(int accoperator, int b_constant, int outmode_left, int outmode_right) {
    uint64_t data = 0;
    data |= ((uint64_t)(accoperator   & 0x07));
    data |= ((uint64_t)(b_constant    & 0x0F)) << 3;
    data |= ((uint64_t)(outmode_left  & 0x0F)) << 7;
    // bits 11..12 left as 0 (vendor gap)
    data |= ((uint64_t)(outmode_right & 0x0F)) << 13;
    return data;
}


}  // namespace hem_test
