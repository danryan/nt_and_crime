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

}  // namespace hem_test
