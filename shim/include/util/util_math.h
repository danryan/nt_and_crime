#pragma once
// Define vendor's traditional include guard so vendor's util/util_math.h
// (pulled via relative include from applets/*.h, e.g. LowerRenz.h's
// `#include "../util/util_math.h"`) becomes a no-op once shim's copy is
// in scope. Without this, vendor's util_math.h would redefine Proportion
// in the same TU as shim's util_math.h (ODR violation). Vendor simfloat
// macros (int2simfloat, simfloat2int, simfloat) are available via
// shim/include/HSUtils.h instead, so suppressing vendor's body does not
// break applets (ADEG, ADSREG, Slew, CVRecV2) that use simfloat.
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

// Host-safe equivalents of the ARM-asm fixed-point multiplies vendor
// util/util_math.h defines with `umull` (vendor lines 89-100). Suppressing the
// vendor body (the guard above) drops them, but POLYLFO's Frames engine
// (frames_poly_lfo.cpp) calls multiply_u32xu32_rshift24, so re-provide both with
// a 64-bit intermediate. On the ARM target the vendor asm versions are used
// (the app TU poisons the guard, but the standalone frames_poly_lfo.o ARM
// compile sees the real vendor header); these host shims only stand in when this
// shadow wins, i.e. the host build. Identical numeric result.
#ifndef SHIM_UTIL_MATH_MULTIPLY
#define SHIM_UTIL_MATH_MULTIPLY
static inline uint32_t multiply_u32xu32_rshift24(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>((static_cast<uint64_t>(a) * b) >> 24);
}
static inline uint32_t multiply_u32xu32_rshift(uint32_t a, uint32_t b, uint32_t shift) {
  return static_cast<uint32_t>((static_cast<uint64_t>(a) * b) >> shift);
}
#endif

// SmoothedValue mirrors vendor util/util_math.h:105 verbatim. The shim suppresses
// the vendor body (the guard above) to avoid the Proportion ODR clash, which also
// drops this header-only template; the O_C apps need it (APP_LORENZ.h:135-138 holds
// four SmoothedValue<int32_t, 16> CV smoothers). Hemisphere applets never use it,
// so re-providing it here is additive and leaves test-applets unaffected.
template <typename T, T smoothing>
struct SmoothedValue {
  SmoothedValue() : value_(0) { }

  T value_;

  T value() const {
    return value_;
  }

  void push(T value) {
    value_ = (value_ * (smoothing - 1) + value) / smoothing;
  }

  void set(T value) {
    value_ = value;
  }
};
