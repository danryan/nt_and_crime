#pragma once

// Hand-ported O_C menu widgets on shim::Graphics.
//
// Why a hand-port and not a vendor compile: vendor OC_menus.h:28 hard-includes
// "src/drivers/display.h" (the Teensy SH1106 driver) via a quote-include that
// -Ishim/include cannot shadow, because a quote-include resolves relative to the
// including file's own directory (the vendor tree) first. So the shim shadows
// OC_menus.h wholesale (the same bare-name precedence the shim uses for OC_DAC.h
// and OC_scales.h) and reimplements the widget LOGIC verbatim on the shim's own
// shim::Graphics through the existing `graphics` global. Vendor display.h,
// weegfx.h, and OC_menus.h are never pulled.
//
// The widget logic mirrors vendor OC_menus.h / OC_menus.cpp byte-for-byte where
// it touches graphics; the only substitutions are:
//   - kDisplayWidth/kDisplayHeight: vendor reads weegfx::Graphics::kWidth/kHeight
//     (128/64); the shim hardcodes those, since it has no weegfx::Graphics.
//   - DrawChord / DrawMiniChord / DrawMask are dropped: neither validation app
//     uses them and they pull vendor OC_chords.h. They can be added later.

#include <cstdint>

#include "OC_bitmaps.h"
#include "util/util_macros.h"      // CONSTRAIN, DISALLOW_COPY_AND_ASSIGN
#include "util/util_settings.h"    // settings::value_attr
#include "OC_DAC.h"                // OC::DAC::get_voltage_scaling, DAC_CHANNEL_*
#include "hem_graphics.h"          // shim::Graphics + weegfx compat namespace

namespace OC {

// Tonnetz note-circle screensaver helper (vendor OC_menus.cpp:65, declared
// OC_menus.h:38). Hand-ported in shim/src/oc/menus.cpp.
void visualize_pitch_classes(uint8_t *normalized, weegfx::coord_t centerx, weegfx::coord_t centery);

// Low-rents screensaver. Reads the shim DAC output-history ring via
// scope_averaging (vendor OC_menus.cpp:116) and plots a Lissajous figure.
// Hand-ported in shim/src/oc/menus.cpp.
void vectorscope_render();

namespace menu {

void Init();

// Vendor reads these from weegfx::Graphics::kWidth/kHeight (OC_menus.h:48-49).
// The shim has no weegfx::Graphics, so the panel geometry is hardcoded to the
// NT's 128x64 logical canvas (the apps draw into [0,128) and the runtime
// centers afterward).
static constexpr weegfx::coord_t kDisplayWidth = 128;
static constexpr weegfx::coord_t kDisplayHeight = 64;
static constexpr weegfx::coord_t kMenuLineH = 12;
static constexpr weegfx::coord_t kFontHeight = 8;
static constexpr weegfx::coord_t kDefaultMenuStartX = 0;
static constexpr weegfx::coord_t kDefaultValueX = 96;
static constexpr weegfx::coord_t kDefaultMenuEndX = kDisplayWidth - 2;
static constexpr weegfx::coord_t kIndentDx = 2;
static constexpr weegfx::coord_t kTextDy = 2;

static constexpr int kScreenLines = 4;

static inline weegfx::coord_t CalcLineY(int line) {
  return (line + 1) * kMenuLineH + 2 + 1;
}

// Cursor position manager for settings-type menus (vendor OC_menus.h:69-146).
// "fancy" mode keeps a one-line margin at the top and bottom of the visible
// window so the user can tell there are more items. Assumes at least 4 items.
template <int screen_lines, bool fancy = true>
class ScreenCursor {
public:
  ScreenCursor() { }
  ~ScreenCursor() { }

  void Init(int start, int end) {
    editing_ = false;
    start_ = start;
    end_ = end;
    cursor_pos_ = start;
    screen_line_ = 0;
  }

  void AdjustEnd(int end) {
    // Specific use case where the screen line is intentionally left unadjusted.
    end_ = end;
  }

  void Scroll(int amount) {
    int pos = cursor_pos_ + amount;
    CONSTRAIN(pos, start_, end_);
    cursor_pos_ = pos;

    int screen_line = screen_line_ + amount;
    if (fancy) {
      if (amount < 0) {
        if (screen_line < 2) {
          if (pos >= start_ + 1)
            screen_line = 1;
          else
            screen_line = 0;
        }
      } else {
        if (screen_line >= screen_lines - 2) {
          if (pos <= end_ - 1)
            screen_line = screen_lines - 2;
          else
            screen_line = screen_lines - 1;
        }
      }
    } else {
      CONSTRAIN(screen_line, 0, screen_lines - 1);
    }
    screen_line_ = screen_line;
  }

  inline int cursor_pos() const { return cursor_pos_; }
  inline int first_visible() const { return cursor_pos_ - screen_line_; }
  inline int last_visible() const {
    return cursor_pos_ - screen_line_ + screen_lines - 1;
  }
  inline bool editing() const { return editing_; }
  inline void toggle_editing() { editing_ = !editing_; }
  inline void set_editing(bool enable) { editing_ = enable; }

private:
  bool editing_;
  int start_, end_;
  int cursor_pos_;
  int screen_line_;
};

// Edit-cursor indicators drawn left of a value being edited (vendor
// OC_menus.cpp:42-60). Hand-ported in shim/src/oc/menus.cpp.
void DrawEditIcon(weegfx::coord_t x, weegfx::coord_t y, int value, int min_value, int max_value);
void DrawEditIcon(weegfx::coord_t x, weegfx::coord_t y, int value, const settings::value_attr &attr);

// Gate/clock activity indicator (vendor OC_menus.h:232). state is a 0..255
// envelope quantized to a 0..64 brightness step, then drawn from the gate
// bitmap.
inline static void DrawGateIndicator(weegfx::coord_t x, weegfx::coord_t y, uint8_t state) {
  state = (state + 3) >> 2;
  if (state)
    graphics.drawBitmap8(x, y, 4, OC::bitmap_gate_indicators_8 + (state << 2));
}

// Multi-column title bar (vendor OC_menus.h:239-270). The Draw() dashed-vs-solid
// divider mirrors the vendor voltage-scaling check; on the shim every channel
// reports 1V/oct, so it draws the solid divider.
template <weegfx::coord_t start_x, int columns, weegfx::coord_t text_dx>
class TitleBar {
public:
  static constexpr weegfx::coord_t kColumnWidth = kDisplayWidth / columns;
  static constexpr weegfx::coord_t kTextX = text_dx;
  static constexpr weegfx::coord_t kTextY = 2;

  inline static void SetColumn(int column) {
    graphics.setPrintPos(start_x + kColumnWidth * column + kTextX, kTextY);
  }

  inline static void Draw() {
    if (OC::DAC::get_voltage_scaling(DAC_CHANNEL_A) || OC::DAC::get_voltage_scaling(DAC_CHANNEL_B) ||
        OC::DAC::get_voltage_scaling(DAC_CHANNEL_C) || OC::DAC::get_voltage_scaling(DAC_CHANNEL_D))
      graphics.drawHLinePattern(start_x, kMenuLineH, kDisplayWidth - start_x, 2);
    else
      graphics.drawHLine(start_x, kMenuLineH, kDisplayWidth - start_x);
    SetColumn(0);
  }

  inline static void Selected(int column) {
    graphics.invertRect(start_x + kColumnWidth * column, 0, kColumnWidth, kMenuLineH - 1);
  }

  inline static void DrawGateIndicator(int column, uint8_t state) {
    menu::DrawGateIndicator(start_x + kColumnWidth * column + 1, 2, state);
  }

  inline static weegfx::coord_t ColumnStartX(int column) {
    return start_x + kColumnWidth * column;
  }
};

// Common, default types (vendor OC_menus.h:273-274).
using DefaultTitleBar = TitleBar<kDefaultMenuStartX, 1, 2>;
using DualTitleBar = TitleBar<kDefaultMenuStartX, 2, 2>;

// A single drawn settings row (vendor OC_menus.h:283-407). The name renders at
// the left, the value (or its enum label) right-aligned at endx, an edit icon
// when editing, and a full-width invert when selected.
struct SettingsListItem {
  bool selected, editing;
  weegfx::coord_t x, y;
  weegfx::coord_t valuex, endx;

  SettingsListItem() { }
  ~SettingsListItem() { }

  inline void DrawName(const settings::value_attr &attr) const {
    graphics.setPrintPos(x + kIndentDx, y + kTextDy);
    graphics.print(attr.name);
  }

  inline void DrawCharName(const char *name_string) const {
    graphics.setPrintPos(x + kIndentDx, y + kTextDy);
    graphics.print(name_string);
  }

  inline void DrawDefault(int value, const settings::value_attr &attr) const {
    DrawName(attr);

    graphics.setPrintPos(endx, y + kTextDy);
    if (attr.value_names)
      graphics.print_right(attr.value_names[value]);
    else
      graphics.pretty_print_right(value);

    if (editing)
      menu::DrawEditIcon(valuex, y, value, attr);
    if (selected)
      graphics.invertRect(x, y, kDisplayWidth - x, kMenuLineH - 1);
  }

  inline void DrawDefault(const char *str, int value, const settings::value_attr &attr) const {
    DrawName(attr);

    graphics.setPrintPos(endx, y + kTextDy);
    graphics.print_right(str);

    if (editing)
      menu::DrawEditIcon(valuex, y, value, attr);
    if (selected)
      graphics.invertRect(x, y, kDisplayWidth - x, kMenuLineH - 1);
  }

  inline void DrawCustom() const {
    if (selected)
      graphics.invertRect(x, y, kDisplayWidth - x, kMenuLineH - 1);
  }

  inline void SetPrintPos() const {
    graphics.setPrintPos(x + kIndentDx, y + kTextDy);
  }

  DISALLOW_COPY_AND_ASSIGN(SettingsListItem);
};

// Iterator over the cursor's visible settings window (vendor OC_menus.h:409-448).
// start_x is the name column left edge, value_x the value column left edge (the
// edit cursor sits left of this), end_x the value column right edge.
template <int screen_lines, weegfx::coord_t start_x, weegfx::coord_t value_x,
          weegfx::coord_t end_x = kDefaultMenuEndX>
class SettingsList {
public:
  SettingsList(const ScreenCursor<screen_lines> &cursor)
    : cursor_(cursor)
    , current_item_(cursor.first_visible())
    , last_item_(cursor.last_visible())
    , y_(CalcLineY(kScreenLines - screen_lines))
  { }

  bool available() const {
    return current_item_ <= last_item_;
  }

  int Next(SettingsListItem &item) {
    item.selected = current_item_ == cursor_.cursor_pos();
    item.editing = item.selected && cursor_.editing();
    item.x = start_x;
    item.y = y_;
    item.valuex = value_x;
    item.endx = end_x;
    y_ += kMenuLineH;
    return current_item_++;
  }

  static void AbsoluteLine(int line, SettingsListItem &item) {
    item.x = start_x;
    item.y = CalcLineY(line);
    item.valuex = value_x;
    item.endx = end_x;
  }

private:
  const ScreenCursor<screen_lines> cursor_;
  int current_item_, last_item_;
  weegfx::coord_t y_;

  DISALLOW_COPY_AND_ASSIGN(SettingsList);
};

}  // namespace menu

}  // namespace OC
