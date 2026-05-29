// Emits Verifier's own glyph table (verifier_logic.h glyph_for) as a JSON map
// of glyph -> 6x8 bitmap (row-major, 48 ints). Single source of truth shared
// with the device render. Stdout is redirected to font.json by the Makefile.
#include <cstdio>
#include "../../plugins/probes/verifier_logic.h"

using namespace verifier;

int main() {
    const char* glyphs = "0123456789+-.#";
    printf("{\n");
    for (const char* g = glyphs; *g; ++g) {
        const Glyph& gl = glyph_for(*g);
        printf("  \"%c\": [", *g);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 6; ++x) {
                int bit = (x < 5) ? ((gl.rows[y] >> (4 - x)) & 1) : 0;
                printf("%d", bit);
                if (!(y == 7 && x == 5)) printf(",");
            }
        }
        printf("]%s\n", g[1] ? "," : "");
    }
    printf("}\n");
    return 0;
}
