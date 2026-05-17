#pragma once

#ifndef NT_HEM_NO_IMPL
#include "hem_shim_impl.h"
#define NT_HEM_NO_IMPL 1
#endif

#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <new>
#include <cstring>
#include "HemisphereApplet.h"
#include "HSIOFrame.h"

namespace hem_shim {


inline void copy_bus_to_frame(int bus_param, int* dst, float* busFrames, int numFrames,
                              const int16_t* v) {
    int bus = v[bus_param];
    if (bus <= 0) { *dst = 0; return; }
    const float* src = busFrames + (bus - 1) * numFrames;
    float sum = 0.0f;
    for (int i = 0; i < numFrames; ++i) sum += src[i];
    float mean = sum / (float)numFrames;
    *dst = (int)(mean * 1536.0f);
}

struct GateRead { bool rising; bool high; };
inline GateRead read_gate(int bus_param, float* busFrames, int numFrames, const int16_t* v,
                          bool& prev_high) {
    int bus = v[bus_param];
    if (bus <= 0) { prev_high = false; return { false, false }; }
    const float* src = busFrames + (bus - 1) * numFrames;
    bool rising = false;
    bool last_high = prev_high;
    for (int i = 0; i < numFrames; ++i) {
        bool high = (src[i] > 0.5f);
        if (high && !last_high) rising = true;
        last_high = high;
    }
    prev_high = last_high;
    return { rising, last_high };
}

inline void write_frame_to_bus(int bus_param, int mode_param, int value_hem,
                               float* busFrames, int numFrames, const int16_t* v) {
    int bus = v[bus_param];
    if (bus <= 0) return;
    float* dst = busFrames + (bus - 1) * numFrames;
    float value_nt = (float)value_hem / 1536.0f;
    bool replace = v[mode_param];
    if (replace) {
        for (int i = 0; i < numFrames; ++i) dst[i] = value_nt;
    } else {
        for (int i = 0; i < numFrames; ++i) dst[i] += value_nt;
    }
}

}  // namespace hem_shim
