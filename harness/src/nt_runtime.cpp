#include "nt_runtime.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>

namespace nt { extern const uint8_t* font_6x8_glyph(char c); }

static inline void set_pixel(int x, int y, int colour) {
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
    int byte_index  = y * 128 + (x >> 1);
    uint8_t mask    = (x & 1) ? 0xf0 : 0x0f;
    uint8_t shifted = (uint8_t)((colour & 0x0f) << ((x & 1) ? 4 : 0));
    NT_screen[byte_index] = (uint8_t)((NT_screen[byte_index] & ~mask) | shifted);
}

extern "C" void NT_drawShapeI(_NT_shape shape, int x0, int y0, int x1, int y1, int colour) {
    switch (shape) {
    case kNT_point: set_pixel(x0, y0, colour); break;
    case kNT_line: {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            set_pixel(x0, y0, colour);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    } break;
    case kNT_box: {
        NT_drawShapeI(kNT_line, x0, y0, x1, y0, colour);
        NT_drawShapeI(kNT_line, x1, y0, x1, y1, colour);
        NT_drawShapeI(kNT_line, x1, y1, x0, y1, colour);
        NT_drawShapeI(kNT_line, x0, y1, x0, y0, colour);
    } break;
    case kNT_rectangle: {
        int lo_x = std::min(x0, x1), hi_x = std::max(x0, x1);
        int lo_y = std::min(y0, y1), hi_y = std::max(y0, y1);
        for (int y = lo_y; y <= hi_y; ++y)
            for (int x = lo_x; x <= hi_x; ++x)
                set_pixel(x, y, colour);
    } break;
    case kNT_circle: {
        int r = (int)std::sqrt((double)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
        int x = r, y = 0, err = 0;
        while (x >= y) {
            set_pixel(x0 + x, y0 + y, colour);
            set_pixel(x0 + y, y0 + x, colour);
            set_pixel(x0 - y, y0 + x, colour);
            set_pixel(x0 - x, y0 + y, colour);
            set_pixel(x0 - x, y0 - y, colour);
            set_pixel(x0 - y, y0 - x, colour);
            set_pixel(x0 + y, y0 - x, colour);
            set_pixel(x0 + x, y0 - y, colour);
            ++y;
            if (err <= 0) { err += 2 * y + 1; }
            else          { --x; err -= 2 * x + 1; }
        }
    } break;
    }
}

extern "C" void NT_drawShapeF(_NT_shape shape, float x0, float y0, float x1, float y1, float colour) {
    NT_drawShapeI(shape, (int)x0, (int)y0, (int)x1, (int)y1, (int)colour);
}

extern "C" void NT_drawText(int x, int y, const char* str, int colour,
                            _NT_textAlignment align, _NT_textSize size) {
    (void)align; (void)size;
    if (!str) return;
    int cx = x;
    for (; *str; ++str) {
        const uint8_t* g = nt::font_6x8_glyph(*str);
        for (int col = 0; col < 6; ++col) {
            uint8_t bits = g[col];
            for (int row = 0; row < 8; ++row) {
                if (bits & (1u << row))
                    set_pixel(cx + col, y + row, colour);
            }
        }
        cx += 6;
    }
}

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
bool shape_rasteriser_is_placeholder() { return true; }
}
