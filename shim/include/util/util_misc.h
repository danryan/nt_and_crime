#pragma once
#include <cstdint>
// Minimal shadow of vendor util/util_misc.h. The BYTEBEATGEN screensaver calls
// util::reverse_byte. The vendor header drags OC_options.h and unrelated structs,
// so the shim provides only the one inline, mirroring vendor util_misc.h:31-37
// byte-for-byte.
namespace util {
inline uint8_t reverse_byte(uint8_t b) {
  return ((b & 0x1)  << 7) | ((b & 0x2)  << 5) |
         ((b & 0x4)  << 3) | ((b & 0x8)  << 1) |
         ((b & 0x10) >> 1) | ((b & 0x20) >> 3) |
         ((b & 0x40) >> 5) | ((b & 0x80)  >> 7);
}
}  // namespace util
