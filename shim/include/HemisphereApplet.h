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
    int  In(int ch)        { return cvmap[ch].In(); }
    int  ViewIn(int ch) const { return HS::frame.inputs[ch]; }
    int  ViewOut(int ch) const { return HS::frame.ViewOut(ch); }
    bool Clock(int ch, bool = false) { return HS::frame.clocked[ch]; }
    bool Gate(int ch)      { return trigmap[ch].Gate(); }
    int  DetentedIn(int ch) {
        int v = In(ch);
        if (v > HEMISPHERE_CENTER_DETENT) return v;
        if (v < -HEMISPHERE_CENTER_DETENT) return v;
        return 0;
    }

    void Out(int ch, int value)         { HS::frame.Out((DAC_CHANNEL)(ch), value); }
    void ClockOut(int ch, int ticks = HEMISPHERE_CLOCK_TICKS) { HS::frame.ClockOut((DAC_CHANNEL)(ch), ticks); }
    void GateOut(int ch, bool high)     { Out(ch, high ? (PULSE_VOLTAGE * ONE_OCTAVE) : 0); }

    int  ProportionCV(int cv, int max_pixels, int max_cv = HEMISPHERE_MAX_CV) const {
        long prop = (long)cv * max_pixels / (max_cv > 0 ? max_cv : 1);
        return constrain((int)prop, 0, max_pixels);
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
    void gfxPixel(int x, int y)                { graphics.setPixel(x + gfx_offset, y); }
    void gfxCircle(int x, int y, int r)        { graphics.drawCircle(x + gfx_offset, y, r); }

    void gfxBitmap(int x, int y, int w, const uint8_t* data) {
        graphics.drawBitmap8(x + gfx_offset, y, w, data);
    }
    void gfxIcon(int x, int y, const uint8_t* data, bool /*clearfirst*/ = false) {
        gfxBitmap(x, y, 8, data);
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

// Convenience alias for applets that reference the global help[] array directly.
#define help (HS::help_strings)
