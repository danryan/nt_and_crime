#pragma once
// Define vendor's traditional include guard so vendor's util/util_math.h
// (pulled via relative include from applets/*.h, e.g. LowerRenz.h's
// `#include "../util/util_math.h"`) becomes a no-op once shim's copy is
// in scope. Without this, vendor's util_math.h would redefine Proportion
// in the same TU as shim's util_math.h (ODR violation). Vendor simfloat
// macros (int2simfloat, simfloat2int, simfloat) are available via
// shim/include/HSUtils.h instead, so suppressing vendor's body does not
// break Phase 4 applets (ADEG, ADSREG, Slew, CVRecV2) that use simfloat.
#ifndef UTIL_MATH_H_
#define UTIL_MATH_H_
#endif
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

// SCALE8_16 mirrors vendor util/util_math.h:164. USAT16 mirrors the ARM
// __USAT(x, 16) intrinsic used by vendor APP_LORENZ.h: saturate-to-unsigned
// 16-bit (clamp into [0, 65535]). Both guarded so deps and applets that
// re-include can omit the headers if already in scope.
#ifndef SCALE8_16
#define SCALE8_16(x) ((((x + 1) << 16) >> 8) - 1)
#endif
#ifndef USAT16
#define USAT16(x) ((x) > 65535 ? 65535 : ((x) < 0 ? 0 : (x)))
#endif
