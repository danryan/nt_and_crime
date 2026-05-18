#pragma once
#include <algorithm>
#include <cstdint>

#ifndef CONSTRAIN
#define CONSTRAIN(x, lo, hi) \
    do { if ((x) < (lo)) (x) = (lo); else if ((x) > (hi)) (x) = (hi); } while (0)
#endif

// Free-function Proportion mirroring vendor util/util_math.h:48. ADSREG's
// inner MiniADSR struct calls Proportion from a context that is not a
// HemisphereApplet method, so the shim provides the same name in the
// global namespace. Semantics match the HemisphereApplet::Proportion
// member: scale numerator by max_value relative to denominator. Vendor
// uses simfloat math; the shim uses a 64-bit intermediate to avoid overflow.
constexpr int Proportion(const int numerator, const int denominator, const int max_value) {
    return denominator == 0 ? 0 : (int)((int64_t)numerator * max_value / denominator);
}
