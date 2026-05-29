// Renders a Verifier view into the sim NT_screen, then emits the screen as an
// unpacked 256x64 JSON array (one int per pixel, 0/1), mimicking the
// firmware-unpacked screenshot the python parser consumes. Mode arg: "numeric"
// (buses 13,14 = +1.000, -0.250) or "scope" (a 32 px-period square).
#include <cstdio>
#include <cstring>
#include <distingnt/api.h>
#include "nt_runtime.h"
#include "../../plugins/probes/verifier_logic.h"

using namespace verifier;

static int pixel(int x, int y) {
    int byte = y * 128 + (x >> 1);
    uint8_t b = NT_screen[byte];
    uint8_t nib = (x & 1) ? (b >> 4) : (b & 0x0f);
    return nib ? 1 : 0;
}

static void dump() {
    printf("[");
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 256; ++x) {
            printf("%d", pixel(x, y));
            if (!(y == 63 && x == 255)) printf(",");
        }
    printf("]\n");
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "numeric";
    nt::reset_runtime();
    if (!strcmp(mode, "scope")) {
        float buf[kScopeWidth];
        for (int i = 0; i < kScopeWidth; ++i) buf[i] = ((i % 32) < 16) ? 1.0f : -1.0f;
        render_scope(buf, kScopeWidth, scope_trigger(buf, kScopeWidth), 5.0f);
    } else {
        const int   buses[2]  = {13, 14};
        const float values[2] = {1.000f, -0.250f};
        render_numeric(buses, values, 2);
    }
    dump();
    return 0;
}
