// Unit test for the consolidated O_C customUI dispatch
// (plugins/apps/oc_customui_dispatch.h). Drives dispatch_custom_ui against a
// recording stub OC::App and asserts the emitted (type, control) per gesture,
// for both map_long_press variants. Push-back is a no-op here (alg.alive stays
// false), isolating the emit path.

#include "catch.hpp"
#include "nt_runtime.h"
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

#include "../../plugins/apps/oc_customui_dispatch.h"
#include "oc_ui_sim.h"

#include <cstdint>

namespace {

using oc_runtime::AppAlgorithm;

// Recorded last button / encoder event. The OC::App handler receives a
// const OC::UI::Event& (forward-declared incomplete); reinterpret_cast to the
// real ::UI::Event (layout-identical) to read its fields, the same bridge the
// per-app TUs use.
struct Rec {
    int btn_type = -1, btn_control = -1;
    int enc_control = -1, enc_value = 0;
    int btn_count = 0, enc_count = 0;
};
Rec& rec() { static Rec r; return r; }

void rec_button(const OC::UI::Event& e) {
    const auto& ev = reinterpret_cast<const ::UI::Event&>(e);
    rec().btn_type = ev.type;
    rec().btn_control = ev.control;
    rec().btn_count++;
}
void rec_encoder(const OC::UI::Event& e) {
    const auto& ev = reinterpret_cast<const ::UI::Event&>(e);
    rec().enc_control = ev.control;
    rec().enc_value = ev.value;
    rec().enc_count++;
}

void s_init() {}
size_t s_storage_size() { return 0; }
size_t s_save(void*) { return 0; }
size_t s_restore(const void*) { return 0; }
void s_app_event(OC::AppEvent) {}
void s_loop() {}
void s_draw_menu() {}
void s_draw_ss() {}
void s_isr() {}

const OC::App* make_rec_app() {
    static OC::App app = {
        0xB1B1, "Rec", s_init, s_storage_size, s_save, s_restore, s_app_event,
        s_loop, s_draw_menu, s_draw_ss, rec_button, rec_encoder, s_isr,
    };
    return &app;
}

// Drive a down edge, advance ticks, then an up edge on `bit` through
// dispatch_custom_ui. Read rec() after.
void press_release(AppAlgorithm& alg, uint16_t bit, uint64_t hold_ticks,
                   bool map_long_press) {
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(bit, 0, 0, 0), map_long_press);
    OC::CORE::ticks += hold_ticks;
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(0, bit, 0, 0), map_long_press);
}

void setup(AppAlgorithm& alg) {
    OC::CORE::ticks = 0;
    nt::reset_runtime();
    oc_runtime::construct(alg, make_rec_app());
    rec() = Rec{};
}

}  // namespace

TEST_CASE("dispatch: short release emits PRESS", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button3, oc_ui_sim::kShortHoldTicks, /*map*/ false);
    REQUIRE(rec().btn_count == 1);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_PRESS);
    REQUIRE(rec().btn_control == OC::CONTROL_BUTTON_UP);  // kNT_button3 -> UP
}

TEST_CASE("dispatch: long release with map=false emits LONG_RELEASE", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button4, oc_ui_sim::kLongHoldTicks, /*map*/ false);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_LONG_RELEASE);
}

TEST_CASE("dispatch: long release with map=true emits LONG_PRESS", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button4, oc_ui_sim::kLongHoldTicks, /*map*/ true);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_LONG_PRESS);
}

TEST_CASE("dispatch: short release with map=true emits PRESS", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    press_release(alg, kNT_button3, oc_ui_sim::kShortHoldTicks, /*map*/ true);
    REQUIRE(rec().btn_type == oc_runtime::EVENT_BUTTON_PRESS);
}

TEST_CASE("dispatch: encoder delta emits ENCODER on the turned encoder", "[oc_dispatch]") {
    AppAlgorithm alg; setup(alg);
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(0, 0, +3, 0), /*map*/ false);
    REQUIRE(rec().enc_count == 1);
    REQUIRE(rec().enc_control == OC::CONTROL_ENCODER_L);
    REQUIRE(rec().enc_value == 3);

    rec() = Rec{};
    oc_runtime::dispatch_custom_ui(
        alg, oc_ui_sim::make_uidata(0, 0, 0, -2), /*map*/ false);
    REQUIRE(rec().enc_control == OC::CONTROL_ENCODER_R);
    REQUIRE(rec().enc_value == -2);
}
