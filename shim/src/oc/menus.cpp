// Hand-ported O_C menu widget implementations on shim::Graphics.
//
// This translation unit is O_C-only. It is NOT in SHIM_CORE_SRCS and is NOT
// included by hem_shim_impl.h, so it never aggregates into a Hemisphere applet
// .o. The O_C runtime (added in a later task) pulls it via the O_C aggregation
// header; the host test links it directly.
//
// Bodies mirror vendor OC_menus.cpp: the DrawEditIcon glyphs, the Lissajous
// vectorscope (scope_averaging reading the shim DAC history ring), and the
// tonnetz note-circle. The bitmap data is shim-owned and minimal, so vendor
// OC_bitmaps.cpp is never pulled.

#include "OC_menus.h"

#include <array>
#include <cmath>
#include <cstddef>

#include "util/util_templates.h"

namespace OC {

// --- Shim-owned bitmap data ---------------------------------------------------
//
// Column-major: byte c is column (x + c); bit i lights row (y + i). These are
// hand-drawn placeholders faithful in SIZE and STRUCTURE to vendor OC_bitmaps;
// pixel fidelity is validated on hardware (the spec's stated risk mitigation).

// Three 5-column edit glyphs back to back: in-range arrow, min-edge, max-edge.
const uint8_t bitmap_edit_indicators_8[] = {
  // in-range: a small left-pointing wedge
  0x08, 0x1c, 0x3e, 0x00, 0x00,
  // at-min: down arrow
  0x10, 0x10, 0x7c, 0x38, 0x10,
  // at-max: up arrow
  0x10, 0x38, 0x7c, 0x10, 0x10,
};

// Gate indicator: 4 columns per brightness step, indexed by (state << 2) with
// state in [0,64]. Sized to cover the full quantized range so DrawGateIndicator
// reads in bounds for any state. Each step is a 4x8 filled block whose height
// grows with brightness, mirroring the vendor envelope ramp.
namespace {
constexpr size_t kGateSteps = 65;  // state 0..64 inclusive
uint8_t make_gate_bitmaps_storage[kGateSteps * 4];

const uint8_t *build_gate_bitmaps() {
  for (size_t step = 0; step < kGateSteps; ++step) {
    // Height grows from 1 row (dim) to 8 rows (bright) across the step range.
    int rows = 1 + static_cast<int>((step * 7) / (kGateSteps - 1));
    uint8_t col = 0;
    for (int r = 0; r < rows && r < 8; ++r) col |= static_cast<uint8_t>(1u << r);
    for (int c = 0; c < 4; ++c) make_gate_bitmaps_storage[step * 4 + c] = col;
  }
  return make_gate_bitmaps_storage;
}
}  // namespace
const uint8_t *const bitmap_gate_indicators_8 = build_gate_bitmaps();

// 8x8 filled disk, plotted at each tonnetz note.
const uint8_t circle_disk_bitmap_8x8[] = {
  0x3c, 0x7e, 0xff, 0xff, 0xff, 0xff, 0x7e, 0x3c,
};

// --- Tonnetz note-circle LUT (vendor OC_menus.cpp:14-35) ----------------------

namespace {
struct coords {
  weegfx::coord_t x, y;
};

constexpr float note_circle_r = 28.f;
constexpr float pi = 3.14159265358979323846f;
constexpr float semitone_radians = (2.f * pi / 12.f);
constexpr float index_to_rads(size_t index) {
  return ((index + 12 - 3) % 12) * semitone_radians;
}

template <size_t index>
constexpr coords generate_circle_coords() {
  return {
    static_cast<weegfx::coord_t>(note_circle_r * cosf(index_to_rads(index))),
    static_cast<weegfx::coord_t>(note_circle_r * sinf(index_to_rads(index)))
  };
}

template <size_t... Is>
constexpr std::array<coords, sizeof...(Is)> generate_circle_pos_lut(util::index_sequence<Is...>) {
  return { generate_circle_coords<Is>()... };
}

const std::array<coords, 12> circle_pos_lut =
    generate_circle_pos_lut(util::make_index_sequence<12>::type());
}  // namespace

void visualize_pitch_classes(uint8_t *normalized, weegfx::coord_t centerx, weegfx::coord_t centery) {
  graphics.drawCircle(centerx, centery, note_circle_r);

  coords last_pos = circle_pos_lut[normalized[0]];
  last_pos.x += centerx;
  last_pos.y += centery;
  for (size_t i = 1; i < 3; ++i) {
    graphics.drawBitmap8(last_pos.x - 3, last_pos.y - 3, 8, OC::circle_disk_bitmap_8x8);
    coords current_pos = circle_pos_lut[normalized[i]];
    current_pos.x += centerx;
    current_pos.y += centery;
    graphics.drawLine(last_pos.x, last_pos.y, current_pos.x, current_pos.y);
    last_pos = current_pos;
  }
  graphics.drawLine(last_pos.x, last_pos.y,
                    circle_pos_lut[normalized[0]].x + centerx,
                    circle_pos_lut[normalized[0]].y + centery);
  graphics.drawBitmap8(last_pos.x - 3, last_pos.y - 3, 8, OC::circle_disk_bitmap_8x8);
}

// --- Vectorscope (vendor OC_menus.cpp:96-164) ---------------------------------

namespace {
constexpr size_t kScopeDepth = 64;  // four-channel (non-Teensy41) form

uint16_t scope_history[DAC::kHistoryDepth];
uint16_t averaged_scope_history[DAC_CHANNEL_LAST][kScopeDepth];
size_t averaged_scope_tail = 0;
int scope_update_channel = 0;

template <size_t size>
inline uint16_t calc_average(const uint16_t *data) {
  uint32_t sum = 0;
  size_t n = size;
  while (n--) sum += *data++;
  return static_cast<uint16_t>(sum / size);
}

template <unsigned rshift, uint16_t bitmask>
void scope_averaging() {
  DAC::getHistory(scope_update_channel, scope_history);
  averaged_scope_history[scope_update_channel][averaged_scope_tail] =
      ((65535U - calc_average<DAC::kHistoryDepth>(scope_history)) >> rshift) & bitmask;

  ++scope_update_channel %= DAC_CHANNEL_LAST;

  if (0 == scope_update_channel) {
    averaged_scope_tail = (averaged_scope_tail + 1) % kScopeDepth;
  }
}
}  // namespace

void vectorscope_render() {
  scope_averaging<10, 0x3f>();

  for (weegfx::coord_t x = 0; x < static_cast<weegfx::coord_t>(kScopeDepth) - 1; ++x) {
    size_t index = (x + averaged_scope_tail + 1) % kScopeDepth;
    graphics.setPixel(averaged_scope_history[0][index], averaged_scope_history[1][index]);
    graphics.setPixel(64 + averaged_scope_history[2][index], averaged_scope_history[3][index]);
  }
}

namespace menu {

void Init() { }

void DrawEditIcon(weegfx::coord_t x, weegfx::coord_t y, int value, int min_value, int max_value) {
  const uint8_t *src = OC::bitmap_edit_indicators_8;
  if (value == max_value)
    src += OC::kBitmapEditIndicatorW * 2;
  else if (value == min_value)
    src += OC::kBitmapEditIndicatorW;

  graphics.drawBitmap8(x - 5, y + 1, OC::kBitmapEditIndicatorW, src);
}

void DrawEditIcon(weegfx::coord_t x, weegfx::coord_t y, int value, const settings::value_attr &attr) {
  const uint8_t *src = OC::bitmap_edit_indicators_8;
  if (value == attr.max_)
    src += OC::kBitmapEditIndicatorW * 2;
  else if (value == attr.min_)
    src += OC::kBitmapEditIndicatorW;

  graphics.drawBitmap8(x - 5, y + 1, OC::kBitmapEditIndicatorW, src);
}

}  // namespace menu

}  // namespace OC
