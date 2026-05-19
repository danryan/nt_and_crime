#include <cstdint>
// Placeholder font: every glyph is a solid 6x8 rectangle.
// Replace once the real NT firmware font has been captured.
namespace nt {
const uint8_t* font_6x8_glyph(char c) {
    static const uint8_t solid[6] = {0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e};
    static const uint8_t blank[6] = {0,0,0,0,0,0};
    if (c < 32 || c > 126) return blank;
    return solid;
}
}
