// O_C apps foundation: per-app runtime (control router, cadence accumulator,
// one-edge-per-tick discipline, centering shift, construct-time sentinel,
// enum-label offset, settings round-trip).
//
// Drives the runtime helpers in plugins/apps/_per_app_runtime.h with a dummy
// OC::App aggregate so the foundation can be validated without depending on
// any real vendor app source. The real apps (Low-rents, Harrington1200) are
// validated separately in later tasks.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "OC_core.h"
#include "OC_config.h"
#include "Arduino.h"
#include "hem_graphics.h"

#include "util/util_settings.h"
#include "UI/ui_events.h"

// The header under test. Lives outside shim/include because it is per-app
// runtime infrastructure, not part of the shim contract. Tests reach it by
// relative path the same way the per-applet runtime is reached.
#include "../../plugins/apps/_per_app_runtime.h"

#include <cstring>
#include <cstdint>

namespace {

// Even x -> HIGH nibble, odd x -> LOW nibble. Matches shim::Graphics put_pixel
// and the NT hardware convention. The centering shift is a raw memmove on the
// NT_screen byte buffer, so the packing is preserved across the shift.
uint8_t shim_pixel(int x, int y) {
    int byte_index = y * 128 + (x >> 1);
    return (x & 1) ? (NT_screen[byte_index] & 0x0f) : (NT_screen[byte_index] >> 4);
}

// Dummy app: keeps counters for isr() and loop() and tracks whether
// DrawMenu / DrawScreensaver was the last draw selected. A test wires its
// thunks into an OC::App aggregate manually (matching the per-app .cpp's
// DECLARE_APP-equivalent construction).
struct DummyAppState {
    int  isr_calls              = 0;
    int  loop_calls             = 0;
    int  draw_menu_calls        = 0;
    int  draw_screensaver_calls = 0;
    bool clocked_seen[OC::DIGITAL_INPUT_LAST] = { false, false, false, false };
    // Number of inner ticks during the most recent step() during which the
    // input was clocked. Used by the one-edge-per-tick test.
    int  clocked_ticks[OC::DIGITAL_INPUT_LAST] = { 0, 0, 0, 0 };
    // Optional pixel to set in DrawMenu, in vendor coords [0,128).
    int  menu_pixel_x = -1;
    int  menu_pixel_y = -1;
    // Bus-routing test hooks: when capture_adc is set, the isr records the
    // value it reads from OC::ADC channel 0 and writes dac_write_value to
    // OC::DAC channel 0.
    bool capture_adc    = false;
    int  adc_seen       = 0;
    uint32_t dac_write_value = 0;
};

DummyAppState g_app_state;

void dummy_init() {}
size_t dummy_storage_size() { return 0; }
size_t dummy_save(void*) { return 0; }
size_t dummy_restore(const void*) { return 0; }
void dummy_handle_app_event(OC::AppEvent) {}
void dummy_loop() { ++g_app_state.loop_calls; }
void dummy_draw_menu() {
    ++g_app_state.draw_menu_calls;
    if (g_app_state.menu_pixel_x >= 0 && g_app_state.menu_pixel_y >= 0) {
        graphics.setPixel(g_app_state.menu_pixel_x, g_app_state.menu_pixel_y);
    }
}
void dummy_draw_screensaver() { ++g_app_state.draw_screensaver_calls; }
void dummy_handle_button_event(const OC::UI::Event&) {}
void dummy_handle_encoder_event(const OC::UI::Event&) {}
void dummy_isr() {
    ++g_app_state.isr_calls;
    for (int i = 0; i < OC::DIGITAL_INPUT_LAST; ++i) {
        if (OC::DigitalInputs::clocked(static_cast<OC::DigitalInput>(i))) {
            g_app_state.clocked_seen[i] = true;
            g_app_state.clocked_ticks[i] += 1;
        }
    }
    if (g_app_state.capture_adc) {
        g_app_state.adc_seen = OC::ADC::value(ADC_CHANNEL_1);
        OC::DAC::set(DAC_CHANNEL_A, g_app_state.dac_write_value);
    }
}

const OC::App* make_dummy_app() {
    static OC::App app = {
        /* id */               0xD0D0,
        /* name */             "Dummy",
        /* Init */             dummy_init,
        /* storageSize */      dummy_storage_size,
        /* Save */             dummy_save,
        /* Restore */          dummy_restore,
        /* HandleAppEvent */   dummy_handle_app_event,
        /* loop */             dummy_loop,
        /* DrawMenu */         dummy_draw_menu,
        /* DrawScreensaver */  dummy_draw_screensaver,
        /* HandleButtonEvent */dummy_handle_button_event,
        /* HandleEncoderEvent */dummy_handle_encoder_event,
        /* isr */              dummy_isr,
    };
    return &app;
}

// A SettingsBase-derived class for the enum-offset and settings round-trip
// tests. Two settings: a no-enum int and a min=3 enum with 5 labels.
class DummySettings : public settings::SettingsBase<DummySettings, 2> {
public:
    enum {
        DUMMY_INT = 0,
        DUMMY_ENUM,
        DUMMY_LAST,
    };
};

const char* const dummy_enum_labels[] = { "a", "b", "c", "d", "e" };

}  // namespace

// Vendor SETTINGS_DECLARE expands to:
//   template <> constexpr settings::value_attr
//     settings::SettingsBase<clazz, last>::value_attr_[] = { ... };
SETTINGS_DECLARE(DummySettings, 2) {
    {  0,  0, 100, "intval",  nullptr,            settings::STORAGE_TYPE_I16 },
    {  3,  3,   7, "enumval", dummy_enum_labels,  settings::STORAGE_TYPE_U8  },
};

namespace {

// Wire helpers for tests that need to drive the runtime directly without a
// real per-app .cpp. The runtime is templated on the app facade; using a
// runtime instance type aliased to the dummy facade keeps tests close to the
// shape per-app code will use.
using oc_runtime::AppAlgorithm;

void reset_test_state() {
    g_app_state = DummyAppState{};
    // Reset the OC input/trigger backing.
    for (int i = 0; i < ADC_CHANNEL_COUNT; ++i) oc_io::set_input(i, 0);
    for (int i = 0; i < OC::DIGITAL_INPUT_LAST; ++i) oc_io::set_trigger(i, false);
    // Drain any leftover rising edges from a prior test by scanning twice.
    OC::DigitalInputs::Scan();
    OC::DigitalInputs::Scan();
    OC::CORE::ticks = 0;
    nt::reset_runtime();
}

}  // namespace

TEST_CASE("cadence accumulator drives isr() at the vendor isr rate", "[oc_runtime][cadence]") {
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // The accumulator holds a long-term cadence of OC_CORE_ISR_FREQ ticks per
    // second against NT's sample rate. Over a sufficiently long window the
    // observed tick count tracks (total_frames * isr_freq) / sample_rate to
    // within one tick (the residue of integer math at the end of the window).
    const int frames_per_step = 48;  // 1 ms at 48 kHz
    const int total_steps     = 100;
    const uint64_t total_frames = static_cast<uint64_t>(frames_per_step) * total_steps;
    const uint64_t expected_total =
        (total_frames * OC_CORE_ISR_FREQ) / NT_globals.sampleRate;

    for (int s = 0; s < total_steps; ++s) {
        oc_runtime::step(alg, frames_per_step);
    }

    const int observed = g_app_state.isr_calls;
    // Allow exactly one tick of slack in either direction (the residue of
    // the cadence accumulator at the end of the window).
    REQUIRE(static_cast<uint64_t>(observed) >= expected_total);
    REQUIRE(static_cast<uint64_t>(observed) <= expected_total + 1);

    // Sanity: OC::CORE::ticks must advance once per isr tick.
    REQUIRE(OC::CORE::ticks == static_cast<uint32_t>(observed));
}

TEST_CASE("step() with sampleRate 0 returns without spinning or advancing",
          "[oc_runtime][cadence]") {
    // Regression: the firmware runs step() during add-algorithm before the
    // audio sample rate is established, so NT_globals.sampleRate can be 0. An
    // unguarded `while (numerator >= sr)` with sr == 0 spins forever (watchdog
    // fault, surfaced on-device as "Failed to add algorithm"). The guard must
    // make step() a no-op until the rate is valid. If the guard is missing,
    // this test hangs (which is itself the failure signal).
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    oc_runtime::sample_rate_override() = 0;  // simulate add-time sampleRate == 0
    oc_runtime::step(alg, 48);               // must return; no infinite loop
    oc_runtime::sample_rate_override() = -1;  // restore: use NT_globals

    // No isr ticks, no tick advance while the rate was unestablished.
    REQUIRE(g_app_state.isr_calls == 0);
    REQUIRE(OC::CORE::ticks == 0u);

    // After the rate is restored, the cadence resumes normally.
    oc_runtime::step(alg, 48);
    REQUIRE(g_app_state.isr_calls > 0);
}

TEST_CASE("one-edge-per-tick: a held-high trigger fires exactly one isr tick", "[oc_runtime][edges]") {
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // Route TR input 1 to bus 1 (default) and set bus 1 high before step().
    // The runtime samples the routed bus, sets the trigger level, scans, and
    // emits one rising-edge tick. Subsequent ticks within the same step see
    // the bus high but the edge already latched: clocked() returns 0.
    oc_io::set_trigger(OC::DIGITAL_INPUT_1, true);

    // One step with enough frames to produce many isr ticks.
    oc_runtime::step(alg, 48);

    // Edge must have been seen exactly once across all the isr ticks in this
    // step, regardless of how many ticks the cadence emitted.
    REQUIRE(g_app_state.clocked_seen[OC::DIGITAL_INPUT_1] == true);
    REQUIRE(g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] == 1);

    // A second step with the bus still high MUST NOT re-fire the edge: the
    // rising-edge latch only triggers on low->high transitions.
    g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] = 0;
    oc_runtime::step(alg, 48);
    REQUIRE(g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] == 0);

    // Drop the trigger low, then high again. The next step must fire ONE
    // edge again.
    oc_io::set_trigger(OC::DIGITAL_INPUT_1, false);
    oc_runtime::step(alg, 48);
    oc_io::set_trigger(OC::DIGITAL_INPUT_1, true);
    g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] = 0;
    oc_runtime::step(alg, 48);
    REQUIRE(g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] == 1);
}

TEST_CASE("step routes CV-in bus into OC::ADC and OC::DAC out onto the CV-out bus", "[oc_runtime][routing]") {
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // The dummy isr captures OC::ADC::value(0) and writes a fixed value to
    // OC::DAC channel 0. Drive a known input on the CV-in 1 default bus and
    // assert (a) the isr saw it and (b) the CV-out 1 default bus carries the
    // DAC value converted back to NT bus space.
    g_app_state.capture_adc = true;
    g_app_state.dac_write_value = OC::DAC::kDacZeroCode; // midpoint code = 0V

    // CV-in 1 routes to bus 1 by default; CV-out 1 routes to bus 13 by default.
    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);
    float* bus1  = nt::bus_pointer(1, numFrames);
    float* bus13 = nt::bus_pointer(13, numFrames);

    // 1.0V on the input bus = 1536 hem units when read into OC::ADC.
    for (int i = 0; i < numFrames; ++i) bus1[i] = 1.0f;

    oc_runtime::step(alg, nt::bus_frames_base(), numFrames);

    // The isr must have read the routed input: 1.0V * 1536 = 1536 hem units.
    REQUIRE(g_app_state.adc_seen == 1536);

    // OC::DAC values are 16-bit codes; the midpoint code is 0V on the bus.
    REQUIRE(bus13[0] == Catch::Approx(0.0f).margin(1e-3));

    // A pitch one octave above 0V: kCodesPerVolt codes above the midpoint -> +1V.
    reset_test_state();
    AppAlgorithm alg2;
    oc_runtime::construct(alg2, make_dummy_app());
    g_app_state.capture_adc = true;
    g_app_state.dac_write_value =
        static_cast<uint32_t>(OC::DAC::kDacZeroCode + OC::DAC::kCodesPerVolt); // +1V
    nt::set_bus_frame_count(numFrames);
    float* b13 = nt::bus_pointer(13, numFrames);
    oc_runtime::step(alg2, nt::bus_frames_base(), numFrames);
    REQUIRE(b13[0] == Catch::Approx(1.0f).margin(1e-3));

    // Full-scale modulation codes map to the bipolar rails, not past them: a
    // full-scale Lorenz-style code (max) is +5V, code 0 is -5V. The earlier
    // pitch-only conversion (/1536) railed these to ~+38V / -5V.
    reset_test_state();
    AppAlgorithm alg3;
    oc_runtime::construct(alg3, make_dummy_app());
    g_app_state.capture_adc = true;
    g_app_state.dac_write_value = OC::DAC::kMaxValue; // 65535 -> ~+5V
    nt::set_bus_frame_count(numFrames);
    float* b13_hi = nt::bus_pointer(13, numFrames);
    oc_runtime::step(alg3, nt::bus_frames_base(), numFrames);
    REQUIRE(b13_hi[0] == Catch::Approx(5.0f).margin(0.01f));

    reset_test_state();
    AppAlgorithm alg4;
    oc_runtime::construct(alg4, make_dummy_app());
    g_app_state.capture_adc = true;
    g_app_state.dac_write_value = 0; // 0 -> -5V
    nt::set_bus_frame_count(numFrames);
    float* b13_lo = nt::bus_pointer(13, numFrames);
    oc_runtime::step(alg4, nt::bus_frames_base(), numFrames);
    REQUIRE(b13_lo[0] == Catch::Approx(-5.0f).margin(0.01f));
}

TEST_CASE("re-routing a CV-in bus via alg.v takes effect live", "[oc_runtime][routing][reroute]") {
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // The routing accessors must read the firmware/host-managed published
    // parameter array (alg.v), not the private v_storage snapshot. construct()
    // seeds alg.v == v_storage, but the firmware (and the harness loader)
    // repoint alg.v to its own backing store after construct. Replicate that
    // here with a separate published array so a write to alg.v does NOT alias
    // back into v_storage; only then does reading v_storage vs alg.v differ.
    static int16_t published[oc_runtime::kMaxParams];
    std::memcpy(published, alg.v_storage, sizeof(published));
    const_cast<int16_t*&>(alg.v) = published;

    g_app_state.capture_adc = true;

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // CV-in 1 defaults to bus 1. Put a distinctive 1.0V on the default bus and
    // a different 2.0V on bus 7 (the new routing target).
    float* default_bus  = nt::bus_pointer(1, numFrames);
    float* rerouted_bus = nt::bus_pointer(7, numFrames);
    for (int i = 0; i < numFrames; ++i) {
        default_bus[i]  = 1.0f;
        rerouted_bus[i] = 2.0f;
    }

    // Re-route CV-in 1 (param index 0) from bus 1 to bus 7 by writing the
    // firmware-managed published array, exactly as a routing param edit does.
    // v_storage stays at the bus-1 default; only alg.v moves to bus 7.
    published[0] = 7;
    REQUIRE(alg.v_storage[0] == 1);  // stale snapshot unchanged

    oc_runtime::step(alg, nt::bus_frames_base(), numFrames);

    // The isr must have read the re-routed bus (2.0V * 1536 = 3072 hem units),
    // proving the routing accessor read alg.v, not the stale v_storage default
    // (which would have yielded 1.0V * 1536 = 1536).
    REQUIRE(g_app_state.adc_seen == 3072);
}

TEST_CASE("step routes a TR-in bus edge so clocked() fires once", "[oc_runtime][routing][edges]") {
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // TR-in 1 routes to bus 5 by default. A rising edge on bus 5 must produce
    // exactly one clocked() tick in the isr, mirroring the one-edge-per-tick
    // discipline.
    const int numFrames = 48;
    nt::set_bus_frame_count(numFrames);
    float* bus5 = nt::bus_pointer(5, numFrames);
    for (int i = 0; i < numFrames; ++i) bus5[i] = 5.0f;  // held high

    oc_runtime::step(alg, nt::bus_frames_base(), numFrames);

    REQUIRE(g_app_state.clocked_seen[OC::DIGITAL_INPUT_1] == true);
    REQUIRE(g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] == 1);

    // Held high through a second step: no new edge.
    g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] = 0;
    oc_runtime::step(alg, nt::bus_frames_base(), numFrames);
    REQUIRE(g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] == 0);

    // Drop low then high again: one edge fires.
    for (int i = 0; i < numFrames; ++i) bus5[i] = 0.0f;
    oc_runtime::step(alg, nt::bus_frames_base(), numFrames);
    for (int i = 0; i < numFrames; ++i) bus5[i] = 5.0f;
    g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] = 0;
    oc_runtime::step(alg, nt::bus_frames_base(), numFrames);
    REQUIRE(g_app_state.clocked_ticks[OC::DIGITAL_INPUT_1] == 1);
}

TEST_CASE("centering shift relocates the menu canvas from [0,128) to [64,192)", "[oc_runtime][centering]") {
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // Have DrawMenu set a single pixel at vendor x=10, y=10.
    g_app_state.menu_pixel_x = 10;
    g_app_state.menu_pixel_y = 10;

    oc_runtime::draw(alg);

    // Pixel must appear at NT x = 10 + 64 = 74, y = 10.
    REQUIRE(shim_pixel(74, 10) == 15);

    // Left margin [0, 64) at that row must be blanked.
    for (int x = 0; x < 64; ++x) {
        REQUIRE(shim_pixel(x, 10) == 0);
    }
    // Right margin [192, 256) at that row must also be blanked.
    for (int x = 192; x < 256; ++x) {
        REQUIRE(shim_pixel(x, 10) == 0);
    }

    // DrawMenu was the selected draw path on a fresh idle counter.
    REQUIRE(g_app_state.draw_menu_calls == 1);
    REQUIRE(g_app_state.loop_calls == 1);
}

TEST_CASE("construct-time sentinel suppresses parameterChanged before first draw", "[oc_runtime][sentinel]") {
    reset_test_state();

    AppAlgorithm alg;
    DummySettings settings;
    settings.InitDefaults();
    oc_runtime::construct(alg, make_dummy_app(), &settings, /*num_settings=*/2);

    // Bumping the int setting via parameterChanged BEFORE any draw() must be
    // suppressed by the sentinel: the firmware fires parameterChanged for
    // every parameter during construct, before the algorithm is registered.
    const int settings_base = oc_runtime::settings_param_base();
    alg.v_storage[settings_base + DummySettings::DUMMY_INT] = 42;
    oc_runtime::parameterChanged(alg, settings_base + DummySettings::DUMMY_INT);
    REQUIRE(settings.get_value(DummySettings::DUMMY_INT) == 0);  // default, unchanged

    // After the first draw(), the sentinel arms. The next parameterChanged
    // call writes through to values_[].
    oc_runtime::draw(alg);
    alg.v_storage[settings_base + DummySettings::DUMMY_INT] = 17;
    oc_runtime::parameterChanged(alg, settings_base + DummySettings::DUMMY_INT);
    REQUIRE(settings.get_value(DummySettings::DUMMY_INT) == 17);
}

TEST_CASE("enum parameter table offsets value_names by min", "[oc_runtime][param-table]") {
    reset_test_state();

    AppAlgorithm alg;
    DummySettings settings;
    settings.InitDefaults();
    oc_runtime::construct(alg, make_dummy_app(), &settings, /*num_settings=*/2);

    // Inspect the generated parameter table: the enum row's enumStrings must
    // start at "d" because value_attr.min_ == 3 (the vendor menu indexes
    // value_names[value] absolutely; NT indexes min-relative).
    const _NT_parameter* params = alg.parameters;
    const int settings_base = oc_runtime::settings_param_base();
    const _NT_parameter& enum_row = params[settings_base + DummySettings::DUMMY_ENUM];

    REQUIRE(enum_row.min == 3);
    REQUIRE(enum_row.max == 7);
    REQUIRE(enum_row.unit == kNT_unitEnum);
    REQUIRE(enum_row.enumStrings != nullptr);
    // The label at the (min - min) = 0 slot must be "d" (vendor value_names[3]).
    REQUIRE(enum_row.enumStrings[0] != nullptr);
    REQUIRE(std::strcmp(enum_row.enumStrings[0], "d") == 0);
    REQUIRE(std::strcmp(enum_row.enumStrings[1], "e") == 0);

    // Non-enum row carries unit kNT_unitNone and a null enumStrings.
    const _NT_parameter& int_row = params[settings_base + DummySettings::DUMMY_INT];
    REQUIRE(int_row.unit == kNT_unitNone);
    REQUIRE(int_row.enumStrings == nullptr);
}

TEST_CASE("serialise / deserialise round-trip restores settings", "[oc_runtime][settings]") {
    reset_test_state();

    AppAlgorithm alg;
    DummySettings settings;
    settings.InitDefaults();
    oc_runtime::construct(alg, make_dummy_app(), &settings, /*num_settings=*/2);

    // Mutate the settings directly via apply_value (bypassing the param table
    // so we exercise the Save/Restore blob path, not the parameterChanged path).
    settings.apply_value(DummySettings::DUMMY_INT, 55);
    settings.apply_value(DummySettings::DUMMY_ENUM, 6);

    // Serialise into a host JSON stream.
    auto stream = nt::make_json_stream();
    stream->openObject();
    oc_runtime::serialise(alg, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();

    // Construct a fresh app with default settings and deserialise.
    DummySettings restored;
    restored.InitDefaults();
    AppAlgorithm alg2;
    oc_runtime::construct(alg2, make_dummy_app(), &restored, /*num_settings=*/2);

    auto parse = nt::make_json_parse(json);
    bool ok = oc_runtime::deserialise(alg2, *parse);
    REQUIRE(ok == true);

    REQUIRE(restored.get_value(DummySettings::DUMMY_INT) == 55);
    REQUIRE(restored.get_value(DummySettings::DUMMY_ENUM) == 6);
}

TEST_CASE("customUi classification distinguishes short press from long release", "[oc_runtime][customui]") {
    reset_test_state();

    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    const int bi = oc_runtime::bit_index(kNT_encoderButtonL);

    // --- Short press: down, then up within the long-press window. ---
    _NT_uiData down{};
    down.controls    = kNT_encoderButtonL;
    down.lastButtons = 0;
    OC::CORE::ticks = 100;
    oc_runtime::customUi(alg, down);

    // held_since must record the down tick (F7 accessor).
    REQUIRE(oc_runtime::held_since_at(alg, bi) == 100);
    // No long press has elapsed yet.
    REQUIRE(oc_runtime::was_long_press_already_emitted(&alg, bi) == false);

    _NT_uiData up{};
    up.controls    = 0;
    up.lastButtons = kNT_encoderButtonL;
    OC::CORE::ticks = 100 + oc_runtime::kLongPressTicks / 2;  // still short
    REQUIRE(oc_runtime::classify_release(&alg, bi) ==
            oc_runtime::EVENT_BUTTON_PRESS);
    oc_runtime::customUi(alg, up);

    // --- Long hold: down, advance past 500 ms, then up. ---
    reset_test_state();
    AppAlgorithm alg2;
    oc_runtime::construct(alg2, make_dummy_app());

    _NT_uiData down2{};
    down2.controls    = kNT_encoderButtonL;
    down2.lastButtons = 0;
    OC::CORE::ticks = 0;
    oc_runtime::customUi(alg2, down2);

    // Advance time past the long-press threshold.
    OC::CORE::ticks = oc_runtime::kLongPressTicks + 1;
    REQUIRE(oc_runtime::was_long_press_already_emitted(&alg2, bi) == true);
    REQUIRE(oc_runtime::classify_release(&alg2, bi) ==
            oc_runtime::EVENT_BUTTON_LONG_RELEASE);

    // last_controls accessor (F7) reflects the most recent controls mask.
    REQUIRE(oc_runtime::last_controls_of(alg2) == kNT_encoderButtonL);
}

TEST_CASE("screensaver engages on the isr-tick timeout, deferred by activity",
          "[oc_runtime][screensaver]") {
    reset_test_state();
    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // Fresh (ticks 0): the menu draws, not the screensaver.
    oc_runtime::draw(alg);
    REQUIRE(g_app_state.draw_menu_calls == 1);
    REQUIRE(g_app_state.draw_screensaver_calls == 0);

    // One tick short of the timeout: still the menu.
    OC::CORE::ticks = oc_runtime::kScreensaverTimeoutTicks - 1;
    oc_runtime::draw(alg);
    REQUIRE(g_app_state.draw_menu_calls == 2);
    REQUIRE(g_app_state.draw_screensaver_calls == 0);

    // At the timeout: the screensaver engages.
    OC::CORE::ticks = oc_runtime::kScreensaverTimeoutTicks;
    oc_runtime::draw(alg);
    REQUIRE(g_app_state.draw_screensaver_calls == 1);

    // Control activity stamps the current tick, deferring the screensaver: a
    // following draw at the same tick is back inside the timeout window.
    _NT_uiData turn{};
    turn.encoders[0] = 1;
    oc_runtime::customUi(alg, turn);
    oc_runtime::draw(alg);
    REQUIRE(g_app_state.draw_menu_calls == 3);
    REQUIRE(g_app_state.draw_screensaver_calls == 1);
}

TEST_CASE("draw caches the footer band and step restores it over an overlay",
          "[oc_runtime][footer]") {
    reset_test_state();
    AppAlgorithm alg;
    oc_runtime::construct(alg, make_dummy_app());

    // DrawMenu paints a pixel inside the bottom kFooterRows band (y in
    // [64 - 16, 64)). After centering, vendor x maps to NT x + 64.
    g_app_state.menu_pixel_x = 10;
    g_app_state.menu_pixel_y = 60;
    oc_runtime::draw(alg);
    const int fx = 10 + 64;
    REQUIRE(shim_pixel(fx, 60) != 0);

    // The firmware paints its helper-text overlay over the footer band after
    // draw() returns. Simulate that by clobbering those rows.
    for (int y = 64 - AppAlgorithm::kFooterRows; y < 64; ++y) {
        std::memset(NT_screen + y * 128, 0, 128);
    }
    REQUIRE(shim_pixel(fx, 60) == 0);

    // One step() restores the cached footer band over the overlay (the sample
    // rate is established so the step body runs through to the restore).
    nt::set_bus_frame_count(32);
    oc_runtime::step(alg, 32);
    REQUIRE(shim_pixel(fx, 60) != 0);

    // After kFooterRestoreSteps further steps the guard lapses (navigate-away);
    // a fresh overlay is then left intact until the next draw().
    for (uint32_t i = 0; i < AppAlgorithm::kFooterRestoreSteps; ++i) {
        oc_runtime::step(alg, 32);
    }
    std::memset(NT_screen + 60 * 128, 0, 128);
    oc_runtime::step(alg, 32);
    REQUIRE(shim_pixel(fx, 60) == 0);
}

TEST_CASE("pitch_to_dac maps to the 16-bit code space and saturates at the rails",
          "[oc_runtime][dac]") {
    using namespace OC::DAC;

    // 0V reference: pitch 0, octave 0 -> the code midpoint.
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, 0, 0) == kDacZeroCode);

    // 1V/oct: one octave up is +kCodesPerVolt codes, whether expressed as an
    // octave_offset or as kIntervalSize pitch units (the two legs must agree).
    const int oct_up = pitch_to_dac(DAC_CHANNEL_A, 0, 1);
    REQUIRE(oct_up == Catch::Approx(kDacZeroCode + kCodesPerVolt).margin(1.0));
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, kIntervalSize, 0) == oct_up);

    // +5V is the top rail (5 octaves up == code 65536, clamped to kMaxValue);
    // anything beyond saturates, it does not wrap.
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, 0, 5) == kMaxValue);
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, 0, 6) == kMaxValue);
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, 0, 100) == kMaxValue);

    // -5V is the bottom rail; below it saturates to 0, not a negative/wrapped code.
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, 0, -5) == 0);
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, 0, -6) == 0);
    REQUIRE(pitch_to_dac(DAC_CHANNEL_A, 0, -100) == 0);
}
