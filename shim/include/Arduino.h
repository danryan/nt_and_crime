#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Arduino's `byte` typedef (uint8_t). Used by Phazerville applets like
// TLNeuron for small unsigned offsets.
using byte = uint8_t;

// POSIX-ish `uint` shorthand. ARM-EABI newlib does not declare it; Xfader
// applet relies on it. Define unconditionally so all Phase 6 applet
// translation units see it.
using uint = unsigned int;

// Teensy-style placement attributes. No-op on host and NT plug-in builds.
// Phase 6 promotes DMAMEM here from vec_osc_prereqs.h so any applet pulling
// Teensy headers (e.g. enigma/TuringMachine.h via EnigmaJr) sees the macro
// as soon as Arduino.h is in scope.
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
// free template provides a stable identifier-form fallback. Defined as
// templates with a single type parameter so mixed-type call sites
// (`min(6*x, 63)`) compile cleanly via implicit conversion.
#undef min
#undef max
template <typename T>
inline T min(T a, T b) { return a < b ? a : b; }
template <typename T>
inline T max(T a, T b) { return a > b ? a : b; }

// Arduino-style micros() shim. Returns the shim's monotonic tick counter
// cast to uint32_t. Used by vendor applets only as a seed source for
// randomSeed; precise wall-clock semantics are not required.
namespace OC { namespace CORE { extern volatile uint32_t ticks; } }
inline uint32_t micros() { return (uint32_t)OC::CORE::ticks; }

// millis() returns OC::CORE::ticks / 1000. Used by HSClockManager tempo
// math and other vendor compat headers that track wall-clock-ish deltas.
inline uint32_t millis() { return (uint32_t)(OC::CORE::ticks / 1000); }

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
