#include "nt_runtime.h"
#include "plugin_loader.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
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

// Single-slot algorithm registry. The harness hosts exactly one algorithm.
static nt::LoadedPlugin* g_slot = nullptr;

// Gray-out side table: maps (algIdx, paramIdx) -> bool.
static std::map<std::pair<int,int>, bool> g_gray_out;

// Sim-binary parameter log: when non-null, NT_setParameterFromUi writes
// "idx value\n" here before calling parameterChanged.
static FILE* g_param_log = nullptr;

namespace nt {
int  num_buses()       { return 64; }
int  bus_frame_count() { return g_bus_frames; }
void set_bus_frame_count(int frames) {
    g_bus_frames = frames;
    g_bus_storage.assign((size_t)num_buses() * (size_t)frames, 0.0f);
}
float* bus_pointer(int bus_index, int numFrames) {
    if (bus_index <= 0 || bus_index > num_buses()) return nullptr;
    // Pointer invariant: callers must call set_bus_frame_count(numFrames) once at
    // setup. We do NOT implicitly resize here because that would invalidate any
    // outstanding bus pointer; audio-callback paths hold these across
    // step() calls. Fail loudly instead.
    assert(numFrames == g_bus_frames && "bus_pointer: numFrames mismatch; call set_bus_frame_count first");
    return &g_bus_storage[(size_t)(bus_index - 1) * (size_t)numFrames];
}
float* bus_frames_base() {
    return g_bus_storage.empty() ? nullptr : &g_bus_storage[0];
}
void reset_runtime() {
    std::memset(NT_screen, 0, sizeof(NT_screen));
    set_bus_frame_count(32);
    g_slot = nullptr;
    g_gray_out.clear();
    g_param_log = nullptr;
    nt::reset_plugin_loader();
}

void set_param_log(FILE* f) {
    g_param_log = f;
}
bool shape_rasteriser_is_placeholder() { return true; }

void register_algorithm(LoadedPlugin* plugin) {
    g_slot = plugin;
}

LoadedPlugin* registered_algorithm(int algIdx) {
    if (algIdx == 0 && g_slot != nullptr) return g_slot;
    return nullptr;
}

bool is_parameter_grayed_out(int algIdx, int paramIdx) {
    auto key = std::make_pair(algIdx, paramIdx);
    auto it = g_gray_out.find(key);
    if (it == g_gray_out.end()) return false;
    return it->second;
}
} // namespace nt

// Count the number of parameters an algorithm has by walking parameters[]
// until we reach the end marker. The _NT_algorithm struct stores the table
// as a pointer; the count comes from _NT_algorithmRequirements.numParameters
// filled by calculateRequirements(). For the harness we derive it from the
// factory's calculateRequirements call.
static int param_count_for(const nt::LoadedPlugin* lp) {
    if (!lp || !lp->factory) return 0;
    _NT_algorithmRequirements req;
    req.numParameters = 0;
    req.sram = 0; req.dram = 0; req.dtc = 0; req.itc = 0;
    lp->factory->calculateRequirements(req, nullptr);
    return (int)req.numParameters;
}

extern "C" {

int32_t NT_algorithmIndex(const _NT_algorithm* algorithm) {
    if (g_slot && g_slot->algorithm == algorithm) return 0;
    return -1;
}

uint32_t NT_algorithmCount(void) {
    return g_slot != nullptr ? 1u : 0u;
}

uint32_t NT_parameterOffset(void) {
    // The host harness has no common-parameter prefix; offset is always zero.
    return 0u;
}

void NT_setParameterFromUi(uint32_t algorithmIndex, uint32_t parameter, int16_t value) {
    // Step 1: look up the algorithm by index.
    nt::LoadedPlugin* lp = nt::registered_algorithm((int)algorithmIndex);
    if (!lp) return;

    // Step 2: bounds-check paramIdx against the algorithm's parameters[] table.
    // NT_parameterOffset() is always 0, so paramIdx == parameter here.
    int paramIdx = (int)parameter - (int)NT_parameterOffset();
    int count    = param_count_for(lp);
    if (paramIdx < 0 || paramIdx >= count) {
        // Out-of-bounds: silently no-op, matching NT firmware's documented forgiveness.
        return;
    }

    // Step 3: write value into the algorithm's v[paramIdx].
    // algorithm->v is const int16_t* in api.h; the host backs it with its own
    // storage (s_param_storage in plugin_loader.cpp) and casts away const here
    // so the harness can update parameter values on behalf of the UI.
    int16_t* writable_v = const_cast<int16_t*>(lp->algorithm->v);
    writable_v[paramIdx] = value;

    // Step 4: log parameter change if a log file is set.
    if (g_param_log) {
        std::fprintf(g_param_log, "%d %d\n", paramIdx, (int)value);
    }

    // Step 5: invoke parameterChanged.
    if (lp->factory->parameterChanged) {
        lp->factory->parameterChanged(lp->algorithm, paramIdx);
    }
}

void NT_setParameterFromAudio(uint32_t algorithmIndex, uint32_t parameter, int16_t value) {
    // Identical to NT_setParameterFromUi. In the real firmware this variant is
    // safe to call from audio-rate callbacks; in the host harness the distinction
    // is moot because there is no real-time thread separation.
    NT_setParameterFromUi(algorithmIndex, parameter, value);
}

void NT_setParameterGrayedOut(uint32_t algorithmIndex, uint32_t parameter, bool gray) {
    // Record the gray state in the side table. Does NOT call parameterChanged.
    auto key = std::make_pair((int)algorithmIndex, (int)parameter);
    g_gray_out[key] = gray;
}

// Stubs for other extern "C" functions declared in api.h that tests may link against.
void     NT_setParameterRange(_NT_parameter* ptr, float init, float min, float max, float step) {
    (void)ptr; (void)init; (void)min; (void)max; (void)step;
}
bool     NT_getSlot(class _NT_slot& slot, uint32_t index) {
    (void)slot; (void)index;
    return false;
}
void     NT_updateParameterDefinition(uint32_t algIdx, uint32_t paramIdx) {
    (void)algIdx; (void)paramIdx;
}
void     NT_updateParameterPages(uint32_t algIdx) {
    (void)algIdx;
}
uint32_t NT_getCpuCycleCount(void) { return 0u; }
void     NT_sendMidiByte(uint32_t dest, uint8_t b0) { (void)dest; (void)b0; }
void     NT_sendMidi2ByteMessage(uint32_t dest, uint8_t b0, uint8_t b1) {
    (void)dest; (void)b0; (void)b1;
}
void     NT_sendMidi3ByteMessage(uint32_t dest, uint8_t b0, uint8_t b1, uint8_t b2) {
    (void)dest; (void)b0; (void)b1; (void)b2;
}
void     NT_sendMidiSysEx(uint32_t dest, const uint8_t* data, uint32_t count, bool end) {
    (void)dest; (void)data; (void)count; (void)end;
}
int      NT_intToString(char* buffer, int32_t value) { (void)buffer; (void)value; return 0; }
int      NT_floatToString(char* buffer, float value, int decimalPlaces) {
    (void)buffer; (void)value; (void)decimalPlaces;
    return 0;
}

} // extern "C"
