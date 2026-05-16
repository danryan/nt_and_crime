#include "nt_runtime.h"
#include <cstring>
#include <cstdint>
#include <vector>

uint8_t NT_screen[128 * 64];

static std::vector<float> g_bus_storage;
static int                g_bus_frames = 32;

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
int  num_buses()       { return 64; }
int  bus_frame_count() { return g_bus_frames; }
void set_bus_frame_count(int frames) {
    g_bus_frames = frames;
    g_bus_storage.assign((size_t)num_buses() * (size_t)frames, 0.0f);
}
float* bus_pointer(int bus_index, int numFrames) {
    if (bus_index <= 0 || bus_index > num_buses()) return nullptr;
    if ((int)g_bus_storage.size() < num_buses() * numFrames)
        set_bus_frame_count(numFrames);
    return &g_bus_storage[(size_t)(bus_index - 1) * (size_t)numFrames];
}
float* bus_frames_base() {
    return g_bus_storage.empty() ? nullptr : &g_bus_storage[0];
}
void reset_runtime() {
    std::memset(NT_screen, 0, sizeof(NT_screen));
    set_bus_frame_count(32);
}
}
