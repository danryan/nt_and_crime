#pragma once
#include <cstdint>
#include <distingnt/api.h>

namespace verifier {

constexpr int kMaxBuses   = 6;
constexpr int kScopeWidth = 256;
constexpr int kGlyphW     = 6;
constexpr int kRowH       = 10;
constexpr int kValueChars = 7;   // sNN.fff
constexpr int kValueX     = 12;  // value glyph origin x (after a 2-digit label)

enum NumericMode { kMean = 0, kMin = 1, kMax = 2, kPkPk = 3 };
enum ViewMode    { kNumeric = 0, kScope = 1 };

struct Reduction {
    double   sum;
    uint32_t count;
    float    vmin;
    float    vmax;
};

inline void reduction_reset(Reduction& r) {
    r.sum = 0.0; r.count = 0; r.vmin = 0.0f; r.vmax = 0.0f;
}

inline void reduction_accumulate(Reduction& r, const float* samples, int n) {
    for (int i = 0; i < n; ++i) {
        float s = samples[i];
        if (r.count == 0) { r.vmin = s; r.vmax = s; }
        else { if (s < r.vmin) r.vmin = s; if (s > r.vmax) r.vmax = s; }
        r.sum += (double)s;
        ++r.count;
    }
}

inline float reduction_value(const Reduction& r, int mode) {
    if (r.count == 0) return 0.0f;
    switch (mode) {
        case kMean: return (float)(r.sum / (double)r.count);
        case kMin:  return r.vmin;
        case kMax:  return r.vmax;
        case kPkPk: return r.vmax - r.vmin;
    }
    return 0.0f;
}

}  // namespace verifier
