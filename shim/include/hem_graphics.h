#pragma once
#include <cstdint>

namespace shim {

class Graphics {
public:
    void setPrintPos(int x, int y) { print_x = x; print_y = y; }
    int  getPrintPosX() const { return print_x; }
    int  getPrintPosY() const { return print_y; }

    void print(const char* s);
    void print(int n);

    // Vendor weegfx::Graphics::printf. Phase 6 Relabi::View calls it for
    // status text. Variadic printf-style format; shim implementation
    // delegates to vsnprintf into a small stack buffer then forwards to
    // print(const char*).
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    void setPixel(int x, int y);
    void drawLine(int x0, int y0, int x1, int y1, uint8_t pattern = 0xFF);
    void drawFrame(int x, int y, int w, int h);
    void drawRect(int x, int y, int w, int h);
    void invertRect(int x, int y, int w, int h);
    void clearRect(int x, int y, int w, int h);
    void drawCircle(int x, int y, int r);
    void drawBitmap8(int x, int y, int w, const uint8_t* data);

private:
    int print_x = 0;
    int print_y = 0;
};

}  // namespace shim

extern shim::Graphics graphics;
