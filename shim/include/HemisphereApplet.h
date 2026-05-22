#pragma once
#include <cstdint>
#include <cmath>
#include "Arduino.h"
#include "OC_core.h"
#include "OC_DAC.h"
#include "OC_strings.h"
#include "util/util_math.h"
#include "HSicons.h"
#include "PhzIcons.h"
#include "HSClockManager.h"
#include "HSUtils.h"
#include "HSIOFrame.h"
#include "CVInputMap.h"
#include "hem_graphics.h"

class HemisphereApplet {
public:
    static int cursor_countdown_arr[HS::APPLET_CURSOR_COUNT];

    virtual const char* applet_name() = 0;
    virtual const uint8_t* applet_icon() { return ZAP_ICON; }
    virtual ~HemisphereApplet() = default;

    virtual void Start() = 0;
    virtual void Reset() {}
    virtual void Controller() = 0;
    virtual void View() = 0;
    virtual uint64_t OnDataRequest() = 0;
    virtual void OnDataReceive(uint64_t data) = 0;
    virtual void OnButtonPress() { CursorToggle(); }
    virtual void OnEncoderMove(int direction) = 0;
    virtual void AuxButton() {}

    void BaseStart(HS::HEM_SIDE side) { hemisphere = side; Start(); }

    bool CursorBlink() const { return (HS::cursor_countdown[hemisphere] > 0); }
    void ResetCursor() { HS::cursor_countdown[hemisphere] = HEMISPHERE_CURSOR_TICKS; }
    void CursorToggle() { HS::enc_edit[hemisphere].isEditing ^= 1; ResetCursor(); }
    inline bool EditMode() const { return HS::enc_edit[hemisphere].isEditing; }

    template <typename T>
    void MoveCursor(T& cursor, int direction, int max) {
        cursor += direction;
        if (cursor < 0) cursor = 0;
        if (cursor > max) cursor = max;
        ResetCursor();
    }

    // I/O
    int  channel_offset() const { return hemisphere * 2; }
    int  In(int ch)        { return cvmap[ch + channel_offset()].In(); }
    int  ViewIn(int ch) const { return HS::frame.inputs[ch + channel_offset()]; }
    int  ViewOut(int ch) const { return HS::frame.ViewOut(ch + channel_offset()); }
    bool Clock(int ch, bool = false) { return HS::frame.clocked[ch + channel_offset()]; }
    // Mirrors vendor HemisphereApplet.h:147. Reports whether In(ch) changed by
    // more than HEMISPHERE_CHANGE_THRESHOLD (= 32 hem units, ~1/8 semitone)
    // since the last step. The shim populates HS::frame.changed_cv in step()
    // by comparing each input against the previous step's value.
    bool Changed(int ch) { return HS::frame.changed_cv[ch + channel_offset()]; }
    bool Gate(int ch)      { return trigmap[ch + channel_offset()].Gate(); }
    int  DetentedIn(int ch) {
        int v = In(ch);
        if (v > HEMISPHERE_CENTER_DETENT) return v;
        if (v < -HEMISPHERE_CENTER_DETENT) return v;
        return 0;
    }

    // Float CV input. Mirrors vendor HemisphereApplet.h:154. Returns the
    // raw input divided by max (default HEMISPHERE_MAX_INPUT_CV = 9216).
    float InF(int ch, int max = HEMISPHERE_MAX_INPUT_CV) {
        return static_cast<float>(In(ch)) / max;
    }

    // Semitone-quantized input. Mirrors vendor HemisphereApplet.h:169 which
    // calls input_quant[ch].Process(In(ch)). The shim does not maintain a
    // hysteresis-tracking input_quant pool; it returns In(ch) rounded to
    // the nearest 128 hem units (1 semitone). Sufficient for vendor applets
    // that use SemitoneIn for pitch input.
    int SemitoneIn(int ch) {
        int v = In(ch);
        return ((v + (v >= 0 ? 64 : -64)) / 128) * 128;
    }

    // Cancel edit mode. Mirrors vendor HSApplication.h:252. Shim does not
    // maintain selected_input_map state; CancelEdit just exits EditMode if
    // active.
    void CancelEdit() {
        if (EditMode()) CursorToggle();
    }

    // Mirrors vendor HemisphereApplet.h Quantize member. Delegates to the
    // HS:: channel pool, offsetting ch by the applet's hemisphere side.
    int Quantize(int ch, int cv, int root = 0, int transpose = 0) {
        return HS::Quantize(ch + channel_offset(), cv, root, transpose);
    }

    void Out(int ch, int value)         { HS::frame.Out((DAC_CHANNEL)(ch + channel_offset()), value); }
    void ClockOut(int ch, int ticks = HEMISPHERE_CLOCK_TICKS) { HS::frame.ClockOut((DAC_CHANNEL)(ch + channel_offset()), ticks); }
    void GateOut(int ch, bool high)     { Out(ch, high ? (PULSE_VOLTAGE * ONE_OCTAVE) : 0); }

    int  ProportionCV(int cv, int max_pixels, int max_cv = HEMISPHERE_MAX_CV) const {
        long prop = (long)cv * max_pixels / (max_cv > 0 ? max_cv : 1);
        return constrain((int)prop, 0, max_pixels);
    }

    // Proportion lives as a free function in util/util_math.h so that vendor
    // applets with nested structs (ADSREG's MiniADSR) can call it without
    // unqualified lookup binding to a non-static member. Methods that
    // previously called Proportion(...) as a member still resolve to the
    // free function via unqualified lookup in the enclosing namespace scope.

    // Bipolar CV modulation of a parameter. Mirrors upstream signature; shim
    // path uses Proportion only (no SemitoneIn quantizer for small ranges).
    template <typename T>
    void Modulate(T& param, const int ch, const int min = 0, const int max = 255) {
        int increment = Proportion(DetentedIn(ch), HEMISPHERE_MAX_INPUT_CV, max);
        int v = (int)param + increment;
        if (v < min) v = min;
        if (v > max) v = max;
        param = (T)v;
    }

    void StartADCLag(int ch = 0, int lag = HEMISPHERE_ADC_LAG) {
        HS::frame.adc_lag_countdown[ch + channel_offset()] = lag;
    }
    bool EndOfADCLag(int ch = 0) {
        return (--HS::frame.adc_lag_countdown[ch + channel_offset()] == 0);
    }

    // gfx wrappers — all draw to the shim's graphics global, with offsets honored.
    // Both axes use a shim global: gfx_offset (X) and gfx_offset_y (Y).
    // Hosts set both before calling per-applet View(); standalone runs leave
    // them at 0 so the applet draws to canonical coordinates.
    //
    // Q1 clip rect: HS::gfx_clip_w / gfx_clip_h bound emissions to the
    // screen-space rectangle [gfx_offset, gfx_offset+clip_w) x
    // [gfx_offset_y, gfx_offset_y+clip_h). Defaults are full screen
    // (256x64). Hosts overwrite per-frame to their slot's column so
    // vendor over-draws past x=63 do not bleed into the neighboring lane.
    // The clamp is per-emit for pixels and bitmaps; rect-shaped primitives
    // (gfxRect, gfxInvert, gfxClear, gfxFrame) intersect the bounding box
    // with the rect and short-circuit when fully outside.
private:
    // True if screen-space pixel (sx, sy) is inside the clip rect.
    static bool clip_contains(int sx, int sy) {
        return sx >= HS::gfx_offset
            && sx <  HS::gfx_offset + HS::gfx_clip_w
            && sy >= HS::gfx_offset_y
            && sy <  HS::gfx_offset_y + HS::gfx_clip_h;
    }
    // Intersect rect (sx, sy, sw, sh) [screen coords] with the clip rect.
    // Returns false if the result is empty; otherwise updates the args
    // in place to the intersected rect.
    static bool clip_intersect_rect(int& sx, int& sy, int& sw, int& sh) {
        int x0 = sx, y0 = sy;
        int x1 = sx + sw, y1 = sy + sh;
        int cx0 = HS::gfx_offset;
        int cy0 = HS::gfx_offset_y;
        int cx1 = HS::gfx_offset + HS::gfx_clip_w;
        int cy1 = HS::gfx_offset_y + HS::gfx_clip_h;
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        if (x0 >= x1 || y0 >= y1) return false;
        sx = x0; sy = y0; sw = x1 - x0; sh = y1 - y0;
        return true;
    }
public:
    void gfxPos(int x, int y)                  { graphics.setPrintPos(x + gfx_offset, y + gfx_offset_y); }
    void gfxPrint(const char* s)               { graphics.print(s); }
    void gfxPrint(int n)                       { graphics.print(n); }
    void gfxPrint(int x, int y, const char* s) { gfxPos(x, y); gfxPrint(s); }
    void gfxPrint(int x, int y, int n)         { gfxPos(x, y); gfxPrint(n); }
    // Vendor 2-arg form: pad left by (x_adv / 6) spaces, then print number.
    void gfxPrint(int x_adv, int n) {
        for (int c = 0; c < (x_adv / 6); c++) gfxPrint(" ");
        gfxPrint(n);
    }

    void gfxFrame(int x, int y, int w, int h)  {
        // Bounding-box short-circuit, then delegate to 4 clipped lines.
        // (graphics.drawFrame is the unclipped 4-line draw; reimplementing
        // via gfxLine ensures edges that exit the clip rect are dropped
        // per-pixel rather than drawn into the neighboring lane.)
        int bx = x + gfx_offset, by = y + gfx_offset_y, bw = w, bh = h;
        if (!clip_intersect_rect(bx, by, bw, bh)) return;
        gfxLine(x,         y,         x + w - 1, y);
        gfxLine(x + w - 1, y,         x + w - 1, y + h - 1);
        gfxLine(x,         y + h - 1, x + w - 1, y + h - 1);
        gfxLine(x,         y,         x,         y + h - 1);
    }
    // Vendor 5-arg overload (HemisphereApplet.h, VectorLFO.h:206): dotted bool.
    // Shim ignores the dotted flag and falls through to solid frame; host
    // tests do not assert on dotted-vs-solid rendering.
    void gfxFrame(int x, int y, int w, int h, bool /*dotted*/) {
        gfxFrame(x, y, w, h);
    }
    void gfxRect(int x, int y, int w, int h)   {
        int sx = x + gfx_offset, sy = y + gfx_offset_y, sw = w, sh = h;
        if (!clip_intersect_rect(sx, sy, sw, sh)) return;
        graphics.drawRect(sx, sy, sw, sh);
    }
    void gfxInvert(int x, int y, int w, int h) {
        int sx = x + gfx_offset, sy = y + gfx_offset_y, sw = w, sh = h;
        if (!clip_intersect_rect(sx, sy, sw, sh)) return;
        graphics.invertRect(sx, sy, sw, sh);
    }
    void gfxClear(int x, int y, int w, int h)  {
        int sx = x + gfx_offset, sy = y + gfx_offset_y, sw = w, sh = h;
        if (!clip_intersect_rect(sx, sy, sw, sh)) return;
        graphics.clearRect(sx, sy, sw, sh);
    }
    void gfxLine(int x, int y, int x2, int y2) {
        // Bresenham with per-pixel clip. Cheap enough that the rare case
        // of a line entirely inside the rect still pays a single compare
        // per pixel.
        int x0 = x + gfx_offset, y0 = y + gfx_offset_y;
        int x1 = x2 + gfx_offset, y1 = y2 + gfx_offset_y;
        int dx =  std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            if (clip_contains(x0, y0)) graphics.setPixel(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void gfxLine(int x, int y, int x2, int y2, bool dashed) {
        // Per-pixel clip with a dashed-bitmask pattern (matches
        // graphics.drawLine's pattern semantics; we re-do the walk to
        // honor the clip rect).
        const uint8_t pattern = dashed ? 0xAA : 0xFF;
        int x0 = x + gfx_offset, y0 = y + gfx_offset_y;
        int x1 = x2 + gfx_offset, y1 = y2 + gfx_offset_y;
        int dx =  std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        int step = 0;
        for (;;) {
            if ((pattern & (1u << (step & 7))) && clip_contains(x0, y0))
                graphics.setPixel(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
            ++step;
        }
    }
    void gfxPixel(int x, int y) {
        int sx = x + gfx_offset, sy = y + gfx_offset_y;
        if (!clip_contains(sx, sy)) return;
        graphics.setPixel(sx, sy);
    }
    void gfxCircle(int x, int y, int r) {
        // Bounding-box short-circuit. The circle's full bounding box is
        // [x-r, x+r] x [y-r, y+r] in vendor coords; after offset, check
        // against the clip rect. If entirely outside, drop. Otherwise
        // delegate; the framebuffer's own [0,256) x [0,64) bounds in
        // graphics.setPixel still apply so off-screen pixels do not
        // corrupt memory, and the visible bleed pattern that Q1 targets
        // (vendor draws at x=64..68) never lands when the bounding box
        // is fully outside the clip rect.
        int bx = x + gfx_offset - r, by = y + gfx_offset_y - r;
        int bw = 2 * r + 1, bh = 2 * r + 1;
        if (!clip_intersect_rect(bx, by, bw, bh)) return;
        graphics.drawCircle(x + gfx_offset, y + gfx_offset_y, r);
    }

    void gfxBitmap(int x, int y, int w, const uint8_t* data) {
        // Per-pixel clip; the loop body mirrors shim::Graphics::drawBitmap8
        // but tests each pixel against the clip rect before emitting.
        const int sx0 = x + gfx_offset;
        const int sy0 = y + gfx_offset_y;
        for (int col = 0; col < w; ++col) {
            uint8_t bits = data[col];
            for (int row = 0; row < 8; ++row) {
                if (!(bits & (1u << row))) continue;
                int px = sx0 + col, py = sy0 + row;
                if (clip_contains(px, py)) graphics.setPixel(px, py);
            }
        }
    }
    void gfxIcon(int x, int y, const uint8_t* data, bool /*clearfirst*/ = false) {
        gfxBitmap(x, y, 8, data);
    }

    void gfxDottedLine(int x, int y, int x2, int y2) {
        gfxLine(x, y, x2, y2, true);
    }

    // Mirrors upstream `gfxDottedLine(x, y, x2, y2, p)` (HSUtils.cpp). Vendor
    // passes p straight to graphics.drawLine as a dash period; the shim
    // drawLine takes a literal 8-bit pattern, so we translate density to a
    // sensible bitmask: p=1 solid, p=2 half-on (default), p=3 sparse.
    void gfxDottedLine(int x, int y, int x2, int y2, uint8_t p) {
        uint8_t pattern;
        if (p <= 1) pattern = 0xFF;
        else if (p == 2) pattern = 0xAA;
        else pattern = 0x88;
        // Inlined walk to honor the per-step pattern with clip.
        int x0 = x + gfx_offset, y0 = y + gfx_offset_y;
        int x1 = x2 + gfx_offset, y1 = y2 + gfx_offset_y;
        int dx =  std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        int step = 0;
        for (;;) {
            if ((pattern & (1u << (step & 7))) && clip_contains(x0, y0))
                graphics.setPixel(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
            ++step;
        }
    }

    // Phazerville-style header: applet name at top of hemisphere half with a
    // dotted underline at y=10. Left side aligned to x=1; right side aligned
    // to the right edge of the 64-px half. Applets reserve y=0..12 in their
    // View() so the header does not clip content.
    void gfxHeader(const char* str, int y = 2) {
        int x = 1;
        if (hemisphere & 1) {
            int len = 0; while (str[len]) ++len;
            x = 62 - len * 6;
        }
        gfxPrint(x, y, str);
        gfxDottedLine(0, y + 8, 62, y + 8);
    }

    void DrawHeader() { gfxHeader(applet_name()); }

    void gfxSkyline() {
        ForEachChannel(ch) {
            int height = ProportionCV(In(ch), 32);
            gfxFrame(23 + (10 * ch), BottomAlign(height), 6, 63);
            height = ProportionCV(ViewOut(ch), 32);
            gfxInvert(3 + (46 * ch), BottomAlign(height), 12, 63);
        }
    }

    void gfxCursor(int x, int y, int w, int h = 9) {
        if (EditMode()) {
            gfxInvert(x, y - h, w, h);
        } else if (CursorBlink()) {
            gfxLine(x, y, x + w - 1, y);
        }
    }

    // Vendor overload taking label string; shim ignores the label and forwards
    // to the integer-height form. Some upstream applets call this signature.
    void gfxCursor(int x, int y, int w, const char* /*str*/, const char* /*extra*/ = nullptr) {
        gfxCursor(x, y, w, 9);
    }

    // Vendor 6-arg overload (HemisphereApplet.h:259): explicit h plus label
    // and extra strings. Shim ignores the labels.
    void gfxCursor(int x, int y, int w, int h, const char* /*str*/, const char* /*extra*/ = nullptr) {
        gfxCursor(x, y, w, h);
    }

    // Vendor "spicy" cursor variant (HemisphereApplet.h:280-303). Animated
    // edit-mode cursor with optional label box. Shim aliases to plain
    // gfxCursor; host tests do not exercise visual differences.
    void gfxSpicyCursor(int x, int y, int w) {
        gfxCursor(x, y, w, 9);
    }
    void gfxSpicyCursor(int x, int y, int w, int h, const char* /*str*/ = nullptr,
                        const char* /*extra*/ = nullptr) {
        gfxCursor(x, y, w, h);
    }
    void gfxSpicyCursor(int x, int y, int w, const char* /*str*/) {
        gfxCursor(x, y, w, 9);
    }

    // Vendor HSUtils.cpp:518 gfxPrintFreqFromPitch. Vendor body computes a
    // frequency string from a pitch using tideslite::ComputePhaseIncrement
    // and prints it. The shim does not link tideslite into Hemispheres.o
    // (the dep is host-test-only). Stub prints the raw pitch value; host
    // tests do not exercise View() rendering, and ARM display fidelity is
    // not under test for the applets that use this helper.
    void gfxPrintFreqFromPitch(int16_t pitch) {
        gfxPrint(pitch);
    }

    // Vendor gfxStartCursor / gfxEndCursor (HemisphereApplet.h:396-420). Used
    // by Combin8::View to bracket a print sequence with a cursor highlight.
    // Shim records the print position at Start and inverts the printed
    // bounding box at End if the cursor is active.
    void gfxStartCursor() {
        cursor_start_x_ = graphics.getPrintPosX();
        cursor_start_y_ = graphics.getPrintPosY();
    }
    void gfxStartCursor(int x, int y) {
        gfxPos(x, y);
        gfxStartCursor();
    }
    void gfxEndCursor(bool is_cursor, bool /*spicy*/ = false,
                      const char* /*extra*/ = nullptr) {
        int w = graphics.getPrintPosX() - cursor_start_x_;
        if (w <= 0) w = 1;
        if (is_cursor) gfxCursor(cursor_start_x_ - gfx_offset, cursor_start_y_ + 8 - gfx_offset_y, w);
    }

    // gfxPrint overloads for input map types. Vendor uses these to print
    // the human-readable source name for a CVInputMap / DigitalInputMap.
    void gfxPrint(CVInputMap& m) { gfxPrint(m.InputName()); }
    void gfxPrint(const CVInputMap& m) { gfxPrint(m.InputName()); }
    void gfxPrint(DigitalInputMap& /*m*/) { gfxPrint("g"); }
    void gfxPrint(const DigitalInputMap& /*m*/) { gfxPrint("g"); }

    // Mirrors vendor HemisphereApplet.h:541-548. Draws a horizontal slider
    // track of length `len` with a 2x8 thumb at `Proportion(value, max_val,
    // len-1)`. View-only; never affects Out().
    void DrawSlider(uint8_t x, uint8_t y, uint8_t len, uint8_t value, uint8_t max_val, bool is_cursor) {
        uint8_t p = is_cursor ? 1 : 3;
        uint8_t w = (uint8_t)Proportion(value, max_val, len - 1);
        gfxDottedLine(x, y + 4, x + len, y + 4, p);
        gfxRect(x + w, y, 2, 8);
        if (EditMode() && is_cursor) gfxInvert(x - 1, y, len + 3, 8);
    }

    // Mirrors upstream HemisphereApplet::ClockCycleTicks. Returns the recorded
    // cycle ticks for the given channel. Shim has no clock multiplier.
    uint32_t ClockCycleTicks(int ch) {
        return HS::frame.cycle_ticks[ch + channel_offset()];
    }

    const char* OutputLabel(int ch) const { return OC::Strings::capital_letters[ch]; }

    // Mirrors vendor HemisphereApplet.h:595. Vendor implementation draws the
    // CVInputMap editor UI. Shim stub is sufficient for host tests (View
    // not exercised) and ARM hardware (UI redraw not asserted).
    void gfxDisplayInputMapEditor() {}

    // Mirrors vendor HemisphereApplet.h:657 AllowRestart. Vendor uses
    // applet_started flag to gate re-Start() on hemisphere reset. Shim
    // does not track this state; stub is a no-op (Start() always runs).
    void AllowRestart() {}

    // Mirrors vendor HSApplication.h IsEditingInputMap. Shim does not
    // maintain selected_input_map state, so always returns false.
    bool IsEditingInputMap() { return false; }

    // Mirrors vendor HSApplication.h:256 EditSelectedInputMap. Returns true
    // if an input map edit was consumed. Shim has no selected_input_map
    // state, so returns false (caller falls through to applet-specific
    // edit handling).
    bool EditSelectedInputMap(int /*direction*/) { return false; }

    // Mirrors vendor HSApplication.h CheckEditInputMapPress. Variadic; the
    // shim ignores all arguments and returns false so callers fall through
    // to their own button-press handling. Defined as a template so any
    // argument list compiles (Combin8 passes four IndexedInput pairs).
    template <typename... Args>
    bool CheckEditInputMapPress(int /*cursor*/, Args&&... /*input_maps*/) {
        return false;
    }

protected:
    virtual void SetHelp() = 0;
    HS::HEM_SIDE hemisphere = HS::LEFT_HEMISPHERE;
    int cursor_start_x_ = 0;
    int cursor_start_y_ = 0;
};

inline void gfxPrintVoltage(int cv) {
    int v = (cv * (NorthernLightModular ? 120 : 100)) / (12 << 7);
    bool neg = (v < 0);
    if (neg) v = -v;
    int wv = v / 100;
    int dv = v - (wv * 100);
    graphics.print(neg ? "-" : "+");
    graphics.print(wv);
    graphics.print(".");
    if (dv < 10) graphics.print("0");
    graphics.print(dv);
    graphics.print("V");
}

// Convenience alias for applets that reference the global help[] array directly.
#define help (HS::help_strings)

extern uint32_t hem_rng_state;
inline int hem_shim_random(int min_inclusive, int max_exclusive) {
    hem_rng_state ^= hem_rng_state << 13;
    hem_rng_state ^= hem_rng_state >> 17;
    hem_rng_state ^= hem_rng_state << 5;
    int range = max_exclusive - min_inclusive;
    if (range <= 0) return min_inclusive;
    return min_inclusive + (int)(hem_rng_state % (uint32_t)range);
}
inline int hem_shim_random(int max_exclusive) { return hem_shim_random(0, max_exclusive); }
#define random(...) hem_shim_random(__VA_ARGS__)

// Arduino-style randomSeed. Vendor applets (ProbabilityDivider) call
// randomSeed(micros()) before generating new loop content; the shim
// re-seeds the xorshift32 RNG. A zero seed is replaced because xorshift
// stalls at 0.
inline void randomSeed(uint32_t seed) {
    hem_rng_state = seed ? seed : 0xDEADBEEFu;
}
