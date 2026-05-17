#pragma once
#include <cstdint>
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
    bool Gate(int ch)      { return trigmap[ch + channel_offset()].Gate(); }
    int  DetentedIn(int ch) {
        int v = In(ch);
        if (v > HEMISPHERE_CENTER_DETENT) return v;
        if (v < -HEMISPHERE_CENTER_DETENT) return v;
        return 0;
    }

    void Out(int ch, int value)         { HS::frame.Out((DAC_CHANNEL)(ch + channel_offset()), value); }
    void ClockOut(int ch, int ticks = HEMISPHERE_CLOCK_TICKS) { HS::frame.ClockOut((DAC_CHANNEL)(ch + channel_offset()), ticks); }
    void GateOut(int ch, bool high)     { Out(ch, high ? (PULSE_VOLTAGE * ONE_OCTAVE) : 0); }

    int  ProportionCV(int cv, int max_pixels, int max_cv = HEMISPHERE_MAX_CV) const {
        long prop = (long)cv * max_pixels / (max_cv > 0 ? max_cv : 1);
        return constrain((int)prop, 0, max_pixels);
    }

    int Proportion(int numerator, int max_n, int max_p) const {
        if (max_n == 0) return 0;
        return (int)((long)numerator * max_p / max_n);
    }

    void StartADCLag(int ch = 0, int lag = HEMISPHERE_ADC_LAG) {
        HS::frame.adc_lag_countdown[ch + channel_offset()] = lag;
    }
    bool EndOfADCLag(int ch = 0) {
        return (--HS::frame.adc_lag_countdown[ch + channel_offset()] == 0);
    }

    // gfx wrappers — all draw to the shim's graphics global, with offsets honored.
    void gfxPos(int x, int y)                  { graphics.setPrintPos(x + gfx_offset, y); }
    void gfxPrint(const char* s)               { graphics.print(s); }
    void gfxPrint(int n)                       { graphics.print(n); }
    void gfxPrint(int x, int y, const char* s) { gfxPos(x, y); gfxPrint(s); }
    void gfxPrint(int x, int y, int n)         { gfxPos(x, y); gfxPrint(n); }

    void gfxFrame(int x, int y, int w, int h)  { graphics.drawFrame(x + gfx_offset, y, w, h); }
    void gfxRect(int x, int y, int w, int h)   { graphics.drawRect(x + gfx_offset, y, w, h); }
    void gfxInvert(int x, int y, int w, int h) { graphics.invertRect(x + gfx_offset, y, w, h); }
    void gfxClear(int x, int y, int w, int h)  { graphics.clearRect(x + gfx_offset, y, w, h); }
    void gfxLine(int x, int y, int x2, int y2) { graphics.drawLine(x + gfx_offset, y, x2 + gfx_offset, y2); }
    void gfxLine(int x, int y, int x2, int y2, bool dashed) {
        graphics.drawLine(x + gfx_offset, y, x2 + gfx_offset, y2, dashed ? 0xAA : 0xFF);
    }
    void gfxPixel(int x, int y)                { graphics.setPixel(x + gfx_offset, y); }
    void gfxCircle(int x, int y, int r)        { graphics.drawCircle(x + gfx_offset, y, r); }

    void gfxBitmap(int x, int y, int w, const uint8_t* data) {
        graphics.drawBitmap8(x + gfx_offset, y, w, data);
    }
    void gfxIcon(int x, int y, const uint8_t* data, bool /*clearfirst*/ = false) {
        gfxBitmap(x, y, 8, data);
    }

    void gfxDottedLine(int x, int y, int x2, int y2) {
        graphics.drawLine(x + gfx_offset, y, x2 + gfx_offset, y2, 0xAA);
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

    const char* OutputLabel(int ch) const { return OC::Strings::capital_letters[ch]; }

protected:
    virtual void SetHelp() = 0;
    HS::HEM_SIDE hemisphere = HS::LEFT_HEMISPHERE;
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
