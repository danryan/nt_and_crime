#pragma once
// Host-shim prerequisites for the VectorOscillator / WaveformManager /
// RelabiManager headers copied verbatim from vendor into shim/include/.
//
// Include this header before any vector_osc or HSRelabiManager header to
// ensure all symbols they reference are in scope:
//
//   #include "vector_osc/vec_osc_prereqs.h"
//   #include "vector_osc/HSVectorOscillator.h"
//   #include "vector_osc/WaveformManager.h"
//   #include "HSRelabiManager.h"
//
// Why this is needed instead of modifying shim/include/util/util_math.h:
// The Phase 5 dep-branch pre-commit hook classifies util_math.h as a
// shared Layer 0 file and hard-rejects any commit that stages it from a
// phase5-dep/* branch. InterpLinear16 and DMAMEM are therefore provided
// here, within the dep-vec-osc allowed surface, instead.
//
// Vendor headers that call InterpLinear16 compile correctly because this
// header is included first (defining the function) before the vendor
// headers' own #include "../util/util_math.h" runs (which does NOT define
// InterpLinear16 in the shim copy).

#include <cstdint>
#include <cstring>
#include "util/util_math.h"
#include "util/util_macros.h"
#include "Arduino.h"
#include "OC_core.h"
#include "HSUtils.h"

// DMAMEM is a Teensy-specific DMA memory placement attribute. On the host
// simulator it is a no-op.
#ifndef DMAMEM
#define DMAMEM
#endif

// InterpLinear16: linear interpolation between two int16 values at a given
// 16-bit fractional phase. Mirrors vendor util/util_math.h:166.
// The shim's util_math.h omits this function because it is only needed by
// the vector_osc subsystem; it lives here to stay within the dep-vec-osc
// allowed surface.
inline int16_t InterpLinear16(int16_t start, int16_t end, uint16_t phase) {
    int32_t delta = (end - start) * phase;
    return static_cast<int16_t>((delta / 65535) + start);
}
