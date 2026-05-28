// Host coverage for the hand-ported O_C menu widgets (shim/include/OC_menus.h
// + shim/src/oc/menus.cpp). The widgets are reimplemented on shim::Graphics, so
// they render through the NT runtime's NT_screen / NT_drawText. These are net-new,
// O_C-only files; they never enter a Hemisphere applet .o.
//
// Pixel reads use the SHIM packing convention (even x = HIGH nibble), the
// opposite of the nt_runtime sim's pixel() helper. See test_draw_shape.cpp.

#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>

#include "OC_menus.h"

#include "util/util_settings.h"

// The widgets live in OC::menu, as the vendor lays them out; the vendor apps
// reach them as `menu::` from inside `namespace OC` or via `using namespace OC`.
// The test mirrors that to exercise the same name lookup the apps rely on.
using namespace OC;

namespace {

// Even x -> HIGH nibble, odd x -> LOW nibble. Matches shim::Graphics put_pixel
// and the NT hardware convention; this is what the menu widgets write through.
uint8_t shim_pixel(int x, int y) {
    int byte_index = y * 128 + (x >> 1);
    return (x & 1) ? (NT_screen[byte_index] & 0x0f) : (NT_screen[byte_index] >> 4);
}

// Any lit pixel in the rectangle [x0,x1) x [y0,y1).
bool any_lit(int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (shim_pixel(x, y)) return true;
    return false;
}

// A tiny SettingsBase with a plain int setting and an enum setting (value_names)
// so SettingsList iteration and SettingsListItem draw paths are exercised.
enum TestSetting {
    TEST_SETTING_LEVEL,
    TEST_SETTING_MODE,
    TEST_SETTING_GAIN,
    TEST_SETTING_PAN,
    TEST_SETTING_LAST
};

const char* const mode_names[] = { "off", "on", "auto" };

class TestSettings : public settings::SettingsBase<TestSettings, TEST_SETTING_LAST> {
public:
    int get_level() const { return values_[TEST_SETTING_LEVEL]; }
};

}  // namespace

SETTINGS_DECLARE(TestSettings, TEST_SETTING_LAST) {
    { 5, 0, 10, "Level", nullptr, settings::STORAGE_TYPE_U8 },
    { 1, 0, 2, "Mode", mode_names, settings::STORAGE_TYPE_U8 },
    { 0, -8, 8, "Gain", nullptr, settings::STORAGE_TYPE_I8 },
    { 0, 0, 100, "Pan", nullptr, settings::STORAGE_TYPE_U8 },
};

TEST_CASE("ScreenCursor scroll clamps to [start, end]", "[oc_menus]") {
    menu::ScreenCursor<menu::kScreenLines> cursor;
    cursor.Init(0, 3);

    REQUIRE(cursor.cursor_pos() == 0);

    cursor.Scroll(-1);  // already at start
    REQUIRE(cursor.cursor_pos() == 0);

    cursor.Scroll(2);
    REQUIRE(cursor.cursor_pos() == 2);

    cursor.Scroll(10);  // past end
    REQUIRE(cursor.cursor_pos() == 3);

    cursor.Scroll(-100);  // before start
    REQUIRE(cursor.cursor_pos() == 0);
}

TEST_CASE("ScreenCursor edit toggle flips editing state", "[oc_menus]") {
    menu::ScreenCursor<menu::kScreenLines> cursor;
    cursor.Init(0, 3);

    REQUIRE_FALSE(cursor.editing());
    cursor.toggle_editing();
    REQUIRE(cursor.editing());
    cursor.toggle_editing();
    REQUIRE_FALSE(cursor.editing());
    cursor.set_editing(true);
    REQUIRE(cursor.editing());
}

TEST_CASE("ScreenCursor AdjustEnd extends the scroll range", "[oc_menus]") {
    menu::ScreenCursor<menu::kScreenLines> cursor;
    cursor.Init(0, 1);
    cursor.Scroll(10);
    REQUIRE(cursor.cursor_pos() == 1);

    cursor.AdjustEnd(5);
    cursor.Scroll(10);
    REQUIRE(cursor.cursor_pos() == 5);
}

TEST_CASE("SettingsList iterates visible items with correct layout", "[oc_menus]") {
    menu::ScreenCursor<menu::kScreenLines> cursor;
    cursor.Init(0, TEST_SETTING_LAST - 1);

    menu::SettingsList<menu::kScreenLines, menu::kDefaultMenuStartX,
                       menu::kDefaultValueX, menu::kDefaultMenuEndX>
        list(cursor);

    int count = 0;
    int first_y = -1;
    int prev_y = -1000;
    while (list.available()) {
        menu::SettingsListItem item;
        int current = list.Next(item);
        REQUIRE(item.x == menu::kDefaultMenuStartX);
        REQUIRE(item.valuex == menu::kDefaultValueX);
        REQUIRE(item.endx == menu::kDefaultMenuEndX);
        if (first_y < 0) first_y = item.y;
        // y advances by kMenuLineH each row.
        if (prev_y != -1000) REQUIRE(item.y == prev_y + menu::kMenuLineH);
        prev_y = item.y;
        // The first item is the selected cursor row.
        if (current == cursor.cursor_pos()) REQUIRE(item.selected);
        ++count;
    }
    // 4 settings, 4 screen lines -> all four visible.
    REQUIRE(count == TEST_SETTING_LAST);
}

TEST_CASE("SettingsListItem DrawDefault renders the setting name", "[oc_menus]") {
    nt::reset_runtime();
    TestSettings s;
    s.InitDefaults();

    menu::ScreenCursor<menu::kScreenLines> cursor;
    cursor.Init(0, TEST_SETTING_LAST - 1);
    menu::SettingsList<menu::kScreenLines, menu::kDefaultMenuStartX,
                       menu::kDefaultValueX, menu::kDefaultMenuEndX>
        list(cursor);

    menu::SettingsListItem item;
    int current = list.Next(item);
    item.DrawDefault(s.get_value(current), TestSettings::value_attr(current));

    // The name "Level" renders left of the value column; some pixels are lit in
    // the name region of this row.
    REQUIRE(any_lit(item.x, item.y, item.valuex, item.y + menu::kMenuLineH));
}

TEST_CASE("SettingsListItem enum DrawDefault renders without overrun", "[oc_menus]") {
    nt::reset_runtime();
    TestSettings s;
    s.InitDefaults();

    menu::SettingsListItem item;
    menu::SettingsList<menu::kScreenLines, menu::kDefaultMenuStartX,
                       menu::kDefaultValueX, menu::kDefaultMenuEndX>::AbsoluteLine(0, item);
    item.selected = true;
    item.editing = true;
    // TEST_SETTING_MODE has value_names; exercise the value_names branch + the
    // edit-icon + selected-invert paths.
    item.DrawDefault(s.get_value(TEST_SETTING_MODE),
                     TestSettings::value_attr(TEST_SETTING_MODE));
    SUCCEED();
}

TEST_CASE("DefaultTitleBar draws a single divider line", "[oc_menus]") {
    nt::reset_runtime();
    menu::DefaultTitleBar::Draw();
    // A horizontal divider runs at y = kMenuLineH across the full width.
    REQUIRE(shim_pixel(menu::kDefaultMenuStartX, menu::kMenuLineH) == 15);
    REQUIRE(shim_pixel(menu::kDisplayWidth - 4, menu::kMenuLineH) == 15);
}

TEST_CASE("DualTitleBar selecting a column inverts its header band", "[oc_menus]") {
    nt::reset_runtime();
    menu::DualTitleBar::Draw();
    // Divider present.
    REQUIRE(shim_pixel(0, menu::kMenuLineH) == 15);

    // Selecting column 1 inverts the right half header band [64,128)x[0,kMenuLineH).
    nt::reset_runtime();
    menu::DualTitleBar::Selected(1);
    REQUIRE(any_lit(menu::kDisplayWidth / 2, 0, menu::kDisplayWidth, menu::kMenuLineH - 1));
    // Column 0 header band stays dark.
    REQUIRE_FALSE(any_lit(0, 0, menu::kDisplayWidth / 2, menu::kMenuLineH - 1));
}

TEST_CASE("vectorscope_render reads DAC history and plots in bounds", "[oc_menus]") {
    nt::reset_runtime();
    // Push some DAC output so the history ring is non-empty.
    for (int i = 0; i < 16; ++i) {
        OC::DAC::set(DAC_CHANNEL_A, 1000 + i * 100);
        OC::DAC::set(DAC_CHANNEL_B, 2000 + i * 100);
        OC::DAC::set(DAC_CHANNEL_C, 3000 + i * 100);
        OC::DAC::set(DAC_CHANNEL_D, 4000 + i * 100);
    }
    // Render many frames; must not fault or write out of bounds (put_pixel
    // clamps, so the assertion is simply that this completes).
    for (int frame = 0; frame < 80; ++frame) {
        OC::vectorscope_render();
    }
    SUCCEED();
}

TEST_CASE("visualize_pitch_classes draws the tonnetz circle in bounds", "[oc_menus]") {
    nt::reset_runtime();
    uint8_t normalized[3] = { 0, 4, 7 };  // C major triad pitch classes
    OC::visualize_pitch_classes(normalized, 64, 32);
    // The circle is drawn around the center; some pixels are lit near it.
    REQUIRE(any_lit(32, 0, 96, 64));
}
