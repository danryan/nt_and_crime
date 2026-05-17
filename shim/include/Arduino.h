#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

template <typename T>
inline T constrain(T x, T lo, T hi) {
    return std::min(std::max(x, lo), hi);
}

#ifndef min
#define min(a, b) std::min((a), (b))
#endif
#ifndef max
#define max(a, b) std::max((a), (b))
#endif
