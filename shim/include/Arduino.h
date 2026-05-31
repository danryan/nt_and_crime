#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Arduino's `byte` typedef (uint8_t). Used by Phazerville applets like
// TLNeuron for small unsigned offsets.
using byte = uint8_t;

// POSIX-ish `uint` shorthand. ARM-EABI newlib does not declare it; Xfader
// applet relies on it. Define unconditionally so all applet translation
// units see it.
using uint = unsigned int;

// Teensy-style placement attributes. No-op on host and NT plug-in builds.
// DMAMEM is defined here (and also in vec_osc_prereqs.h) so any applet
// pulling Teensy headers (e.g. enigma/TuringMachine.h via EnigmaJr) sees
// the macro as soon as Arduino.h is in scope.
#ifndef DMAMEM
#define DMAMEM
#endif
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef FLASHMEM
#define FLASHMEM
#endif
#ifndef EXTMEM
#define EXTMEM
#endif
#ifndef FASTRUN
#define FASTRUN
#endif

#include <type_traits>
template <typename T, typename U, typename V>
inline auto constrain(T x, U lo, V hi) -> typename std::common_type<T, U, V>::type {
    using R = typename std::common_type<T, U, V>::type;
    R xr = static_cast<R>(x), lr = static_cast<R>(lo), hr = static_cast<R>(hi);
    return xr < lr ? lr : (xr > hr ? hr : xr);
}

// Arduino-style min/max as free templates. Vendor applets call `min(a, b)`
// and `max(a, b)` as ordinary identifiers. c++17 libc++ <algorithm> may
// #undef any `min`/`max` macros after Arduino.h is included, so a global
// free template provides a stable identifier-form fallback. Two type
// parameters so mixed-type call sites (`min(uint8_t, int)`,
// `min(6 * col_width, 63)`) compile cleanly via std::common_type.
#undef min
#undef max
template <typename T, typename U>
inline auto min(T a, U b) -> typename std::common_type<T, U>::type {
    using R = typename std::common_type<T, U>::type;
    return static_cast<R>(a) < static_cast<R>(b) ? static_cast<R>(a) : static_cast<R>(b);
}
template <typename T, typename U>
inline auto max(T a, U b) -> typename std::common_type<T, U>::type {
    using R = typename std::common_type<T, U>::type;
    return static_cast<R>(a) > static_cast<R>(b) ? static_cast<R>(a) : static_cast<R>(b);
}

// Arduino-style micros() shim. Returns the shim's monotonic tick counter
// cast to uint32_t. Used by vendor applets only as a seed source for
// randomSeed; precise wall-clock semantics are not required.
namespace OC { namespace CORE { extern volatile uint32_t ticks; } }
inline uint32_t micros() { return (uint32_t)OC::CORE::ticks; }

// millis() returns OC::CORE::ticks / 1000. Used by HSClockManager tempo
// math and other vendor compat headers that track wall-clock-ish deltas.
inline uint32_t millis() { return (uint32_t)(OC::CORE::ticks / 1000); }

// Arduino-style random(). Vendor O_C apps call random(howbig) -> [0, howbig)
// and random(howsmall, howbig) -> [howsmall, howbig) for non-deterministic
// content (Automatonnetz randomizes its grid cell transforms). The host libc
// declares only `long random(void)`; these are arity-distinct overloads that
// coexist with it. A small self-contained LCG keeps host tests reproducible and
// behaves identically on ARM, which has no libc `random` at all.
//
// Guarded by #ifndef random because the Hemisphere applet path defines
// `random` as a function-like macro (HemisphereApplet.h -> hem_shim_random). A
// Hemisphere TU that pulls Arduino.h after HemisphereApplet.h would otherwise
// rewrite these definitions through that macro. O_C app TUs never include
// HemisphereApplet.h, so the macro is undefined there and these compile.
#ifndef random
namespace shim_detail {
inline uint32_t& rng_state() {
    static uint32_t s = 0x12345678u;
    return s;
}
inline uint32_t rng_next() {
    uint32_t& s = rng_state();
    s = s * 1664525u + 1013904223u;
    return s;
}
}  // namespace shim_detail

inline long random(long howbig) {
    if (howbig <= 0) return 0;
    return static_cast<long>(shim_detail::rng_next() % static_cast<uint32_t>(howbig));
}
inline long random(long howsmall, long howbig) {
    if (howbig <= howsmall) return howsmall;
    return howsmall + random(howbig - howsmall);
}
#endif  // random

// Arduino elapsedMillis idiom. Construct captures the current millis();
// implicit-cast to uint32_t returns (millis() - base). Assignment resets
// the baseline so the value reads as the assigned figure. Used by vendor
// applets (VectorLFO, Tuner) for tick-delta tracking.
class elapsedMillis {
public:
    elapsedMillis() : base_(millis()) {}
    elapsedMillis(uint32_t v) : base_(millis() - v) {}
    operator uint32_t() const { return millis() - base_; }
    elapsedMillis& operator=(uint32_t v) { base_ = millis() - v; return *this; }
    elapsedMillis& operator+=(uint32_t v) { base_ -= v; return *this; }
    elapsedMillis& operator-=(uint32_t v) { base_ += v; return *this; }
private:
    uint32_t base_;
};
