#pragma once
#include <distingnt/api.h>
#include <cstdint>
#include <cstring>

namespace nt_hem {

static uint8_t g_capture[128 * 64];
static bool    g_pending = false;

static inline void _nt_hem_dump_screen() {
    std::memcpy(g_capture, NT_screen, sizeof(g_capture));
    g_pending = true;
}

constexpr uint8_t kManufacturerId = 0x7D;  // educational/private use byte
constexpr uint8_t kCmdDumpRequest = 0x01;
constexpr uint8_t kCmdDumpReply   = 0x02;

static inline void emit_capture_if_pending(uint32_t destination = kNT_destinationUSB) {
    if (!g_pending) return;
    constexpr uint32_t kChunkBytes = 256;
    const uint32_t total = (sizeof(g_capture) + kChunkBytes - 1) / kChunkBytes;
    uint8_t header[4] = { kManufacturerId, kCmdDumpReply, 0, (uint8_t)total };
    for (uint32_t s = 0; s < total; ++s) {
        header[2] = (uint8_t)s;
        NT_sendMidiSysEx(destination, header, sizeof(header), false);
        const uint32_t offset = s * kChunkBytes;
        const uint32_t this_chunk =
            (offset + kChunkBytes <= sizeof(g_capture)) ? kChunkBytes
                                                        : sizeof(g_capture) - offset;
        NT_sendMidiSysEx(destination, g_capture + offset, this_chunk, s == total - 1);
    }
    g_pending = false;
}

} // namespace nt_hem
