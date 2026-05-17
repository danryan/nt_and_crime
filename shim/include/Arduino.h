#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Arduino's `byte` typedef (uint8_t). Used by Phazerville applets like
// TLNeuron for small unsigned offsets.
using byte = uint8_t;

#include <type_traits>
template <typename T, typename U, typename V>
inline auto constrain(T x, U lo, V hi) -> typename std::common_type<T, U, V>::type {
    using R = typename std::common_type<T, U, V>::type;
    R xr = static_cast<R>(x), lr = static_cast<R>(lo), hr = static_cast<R>(hi);
    return xr < lr ? lr : (xr > hr ? hr : xr);
}

#ifndef min
#define min(a, b) std::min((a), (b))
#endif
#ifndef max
#define max(a, b) std::max((a), (b))
#endif
