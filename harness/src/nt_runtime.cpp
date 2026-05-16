#include "nt_runtime.h"
#include <cstring>
#include <cstdint>

uint8_t NT_screen[128 * 64];

static float g_work_buffer[64 * 1024 / sizeof(float)];
const _NT_globals NT_globals = {
    .sampleRate           = 48000u,
    .maxFramesPerStep     = 64u,
    .workBuffer           = g_work_buffer,
    .workBufferSizeBytes  = sizeof(g_work_buffer),
    .streamSizeBytes      = 0u,
    .streamBufferSizeBytes= 0u,
};

namespace nt {
void reset_runtime() {
    std::memset(NT_screen, 0, sizeof(NT_screen));
}
}
