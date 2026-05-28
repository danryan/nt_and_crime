#pragma once
// Define the vendor include guard so a quote-include of "OC_bitmaps.h" from
// inside a vendor app header (APP_H1200.h:28, which resolves to its vendor
// sibling first, not through -Ishim/include) becomes a no-op once this shim
// shadow has been included ahead of the vendor app header. Without this the
// vendor OC_bitmaps.h would redefine kBitmapEditIndicatorW and redeclare
// bitmap_gate_indicators_8 with a clashing type. Same poison technique as
// OC_digital_inputs.h.
#ifndef OC_BITMAPS_H_
#define OC_BITMAPS_H_
#endif
#include <cstdint>

// Minimal shim shadow of vendor OC_bitmaps.h. The bare-name include from a
// vendor app or the hand-ported menu widgets resolves here through
// -Ishim/include before the vendor tree, the same shadowing the shim uses for
// OC_DAC.h / OC_menus.h.
//
// Only the icon symbols the hand-ported menu widgets actually reference are
// declared, with tiny shim-owned bitmap data defined in shim/src/oc/menus.cpp,
// so the shim never pulls vendor OC_bitmaps.cpp. Harrington 1200 includes this
// header but calls no bitmap draw method, so the stub satisfies its include.
//
// Each "8" bitmap is a column-major run of bytes; bit i of column byte c lights
// row (y + i) at column (x + c), matching shim::Graphics::drawBitmap8.

namespace OC {

// Edit-cursor indicator drawn left of a value being edited (vendor
// OC_bitmaps.h:36). kBitmapEditIndicatorW columns per glyph; three glyphs back
// to back: middle (in range), min-edge, max-edge. DrawEditIcon advances by
// kBitmapEditIndicatorW into this run.
static constexpr int16_t kBitmapEditIndicatorW = 5;
extern const uint8_t bitmap_edit_indicators_8[];

// Gate/clock activity indicator (vendor OC_bitmaps.h:39). Four columns per
// brightness step; DrawGateIndicator offsets by (state << 2). Declared as a
// pointer (not an array) because the shim definition builds the run at static
// init; the only consumer (DrawGateIndicator) indexes it as
// `bitmap_gate_indicators_8 + (state << 2)`, which a pointer satisfies.
extern const uint8_t *const bitmap_gate_indicators_8;

// 8x8 filled disk plotted at each tonnetz note in visualize_pitch_classes
// (vendor OC_bitmaps.h:44).
extern const uint8_t circle_disk_bitmap_8x8[];

}  // namespace OC
