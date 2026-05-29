#pragma once
#include <cstdint>
#include <distingnt/api.h>

namespace verifier {

constexpr int kMaxBuses   = 6;
constexpr int kScopeWidth = 256;
constexpr int kGlyphW     = 6;
constexpr int kRowH       = 10;
constexpr int kValueChars = 7;   // sNN.fff
constexpr int kValueX     = 12;  // value glyph origin x (after a 2-digit label)

enum NumericMode { kMean = 0, kMin = 1, kMax = 2, kPkPk = 3 };
enum ViewMode    { kNumeric = 0, kScope = 1 };

struct Reduction {
    double   sum;
    uint32_t count;
    float    vmin;
    float    vmax;
};

inline void reduction_reset(Reduction& r) {
    r.sum = 0.0; r.count = 0; r.vmin = 0.0f; r.vmax = 0.0f;
}

inline void reduction_accumulate(Reduction& r, const float* samples, int n) {
    for (int i = 0; i < n; ++i) {
        float s = samples[i];
        if (r.count == 0) { r.vmin = s; r.vmax = s; }
        else { if (s < r.vmin) r.vmin = s; if (s > r.vmax) r.vmax = s; }
        r.sum += (double)s;
        ++r.count;
    }
}

inline float reduction_value(const Reduction& r, int mode) {
    if (r.count == 0) return 0.0f;
    switch (mode) {
        case kMean: return (float)(r.sum / (double)r.count);
        case kMin:  return r.vmin;
        case kMax:  return r.vmax;
        case kPkPk: return r.vmax - r.vmin;
    }
    return 0.0f;
}

inline int volts_to_millivolts(float v) {
    return (int)(v * 1000.0f + (v >= 0.0f ? 0.5f : -0.5f));
}

// Writes the 7-char fixed-width form sNN.fff plus a NUL into out[8].
// Overflow (|mv| > 99999) yields a sentinel with '#' glyphs.
inline void format_mv(int mv, char out[8]) {
    char sign = mv < 0 ? '-' : '+';
    long a = mv < 0 ? -(long)mv : (long)mv;
    if (a > 99999) {
        out[0] = sign;
        out[1] = '#'; out[2] = '#'; out[3] = '#';
        out[4] = '#'; out[5] = '#'; out[6] = '#';
        out[7] = 0;
        return;
    }
    int ip = (int)(a / 1000);   // 0..99
    int fr = (int)(a % 1000);   // 0..999
    out[0] = sign;
    out[1] = (char)('0' + (ip / 10));
    out[2] = (char)('0' + (ip % 10));
    out[3] = '.';
    out[4] = (char)('0' + (fr / 100));
    out[5] = (char)('0' + ((fr / 10) % 10));
    out[6] = (char)('0' + (fr % 10));
    out[7] = 0;
}

// Fills buf left-to-right, keeping every decim-th sample, until kScopeWidth
// samples are captured, then freezes (one-shot). Re-arm by resetting wr/phase/
// filled to 0/0/false.
inline void scope_push(float* buf, int& wr, int& phase, bool& filled,
                       const float* samples, int n, int decim) {
    if (filled) return;
    if (decim < 1) decim = 1;
    for (int i = 0; i < n; ++i) {
        if (phase == 0 && wr < kScopeWidth) buf[wr++] = samples[i];
        phase = (phase + 1) % decim;
        if (wr >= kScopeWidth) { filled = true; break; }
    }
}

// Returns the index of the first rising zero-crossing (prev < 0 <= cur), or 0
// when none exists (untriggered fallback for DC or silence).
inline int scope_trigger(const float* buf, int len) {
    for (int i = 1; i < len; ++i) {
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) return i;
    }
    return 0;
}

inline int volts_to_y(float v, float vscale) {
    if (vscale <= 0.0f) vscale = 5.0f;
    float t = 0.5f - (v / (2.0f * vscale));   // +vscale -> top (0), -vscale -> bottom
    int y = (int)(t * 63.0f + 0.5f);
    if (y < 0) y = 0;
    if (y > 63) y = 63;
    return y;
}

inline void render_numeric(const int* buses, const float* values, int count) {
    char buf[8];
    char lbl[3];
    for (int row = 0; row < count; ++row) {
        int y = row * kRowH + 8;
        lbl[0] = (char)('0' + (buses[row] / 10) % 10);
        lbl[1] = (char)('0' + (buses[row] % 10));
        lbl[2] = 0;
        NT_drawText(0, y, lbl);
        format_mv(volts_to_millivolts(values[row]), buf);
        NT_drawText(kValueX, y, buf);
    }
}

inline void render_scope(const float* buf, int len, int trig, float vscale) {
    int prev_y = volts_to_y(buf[trig % len], vscale);
    for (int x = 1; x < len && x < 256; ++x) {
        int idx = (trig + x) % len;
        int y = volts_to_y(buf[idx], vscale);
        NT_drawShapeI(kNT_line, x - 1, prev_y, x, y);
        prev_y = y;
    }
}

}  // namespace verifier
