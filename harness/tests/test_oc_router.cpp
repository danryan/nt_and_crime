// O_C apps foundation: control-router unit test.
//
// Foundation-level coverage of the per-app runtime's control router using the
// reusable _NT_uiData synthesis helper (harness/include/oc_ui_sim.h). The
// router primitives under test live in plugins/apps/_per_app_runtime.h:
// classify_release, was_long_press_already_emitted, held_since_at,
// last_controls_of, the button_mapping_table, and kLongPressTicks.
//
// The actual UI::Event emission happens in the per-app .cpp (a deliberate
// design split: vendor ui_events.h puts Event in top-level ::UI::, which the
// runtime header does not pull). So these tests assert on the RUNTIME
// PRIMITIVES the per-app TU reads, not on emitted events. The per-app router
// tests (Tasks 1.1, 1.2) reuse the same helper to drive their own
// HandleButtonEvent / HandleEncoderEvent.

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

#include "../../plugins/apps/_per_app_runtime.h"
#include "oc_ui_sim.h"

#include <cstring>
#include <cstdint>

namespace {

// Minimal dummy OC::App. The router tracks state on the AppAlgorithm without
// touching the app body, but customUi() early-returns when alg.app is null, so
// a non-null app must be wired even for the pure-router tests.
void rt_init() {}
size_t rt_storage_size() { return 0; }
size_t rt_save(void*) { return 0; }
size_t rt_restore(const void*) { return 0; }
void rt_handle_app_event(OC::AppEvent) {}
void rt_loop() {}
void rt_draw_menu() {}
void rt_draw_screensaver() {}
void rt_handle_button_event(const OC::UI::Event&) {}
void rt_handle_encoder_event(const OC::UI::Event&) {}
void rt_isr() {}

const OC::App* make_router_app() {
    static OC::App app = {
        /* id */                0xA0A0,
        /* name */              "Router",
        /* Init */              rt_init,
        /* storageSize */       rt_storage_size,
        /* Save */              rt_save,
        /* Restore */           rt_restore,
        /* HandleAppEvent */    rt_handle_app_event,
        /* loop */              rt_loop,
        /* DrawMenu */          rt_draw_menu,
        /* DrawScreensaver */   rt_draw_screensaver,
        /* HandleButtonEvent */ rt_handle_button_event,
        /* HandleEncoderEvent */rt_handle_encoder_event,
        /* isr */               rt_isr,
    };
    return &app;
}

using oc_runtime::AppAlgorithm;

void reset_router_state() {
    for (int i = 0; i < ADC_CHANNEL_COUNT; ++i) oc_io::set_input(i, 0);
    for (int i = 0; i < OC::DIGITAL_INPUT_LAST; ++i) oc_io::set_trigger(i, false);
    OC::DigitalInputs::Scan();
    OC::DigitalInputs::Scan();
    OC::CORE::ticks = 0;
    nt::reset_runtime();
}

}  // namespace

TEST_CASE("router: short press-and-release classifies as a short press", "[oc_router][press]") {
    reset_router_state();
    AppAlgorithm alg;
    oc_runtime::construct(alg, make_router_app());

    // press() drives down then up within the long-press window, advancing
    // OC::CORE::ticks by a small amount in between.
    oc_ui_sim::press(alg, kNT_button3);

    // The release classifies as the vendor short-press event.
    REQUIRE(oc_ui_sim::last_release_class() == oc_runtime::EVENT_BUTTON_PRESS);
}

TEST_CASE("router: a hold past the long-press threshold classifies as a long release", "[oc_router][long-press]") {
    reset_router_state();
    AppAlgorithm alg;
    oc_runtime::construct(alg, make_router_app());

    // long_press() drives down, advances OC::CORE::ticks past kLongPressTicks
    // while the button is held, then drives up. During the hold the runtime
    // reports the long-press already emitted; the release classifies as a
    // long release.
    oc_ui_sim::long_press(alg, kNT_encoderButtonL);

    // During the hold (sampled by the helper just before release) the runtime
    // saw the long press elapse.
    REQUIRE(oc_ui_sim::last_long_press_seen() == true);
    REQUIRE(oc_ui_sim::last_release_class() == oc_runtime::EVENT_BUTTON_LONG_RELEASE);
}

TEST_CASE("router: encoder delta reaches customUi via the synthesized _NT_uiData", "[oc_router][encoder]") {
    reset_router_state();
    AppAlgorithm alg;
    oc_runtime::construct(alg, make_router_app());

    // turn_encoder(L, +3) synthesizes a _NT_uiData with encoders[0] == 3 and
    // feeds it through the runtime customUi. The runtime does not store the
    // delta (the per-app TU reads data.encoders[] directly to emit
    // EVENT_ENCODER), so the test asserts the delta on the snapshot the helper
    // passed in.
    oc_ui_sim::turn_encoder(alg, oc_ui_sim::ENCODER_L, +3);
    REQUIRE(oc_ui_sim::last_uidata().encoders[0] == 3);
    REQUIRE(oc_ui_sim::last_uidata().encoders[1] == 0);

    oc_ui_sim::turn_encoder(alg, oc_ui_sim::ENCODER_R, -2);
    REQUIRE(oc_ui_sim::last_uidata().encoders[1] == -2);
    REQUIRE(oc_ui_sim::last_uidata().encoders[0] == 0);
}

TEST_CASE("router: a chord carries both held bits in last_controls_of (the event mask)", "[oc_router][chord]") {
    reset_router_state();
    AppAlgorithm alg;
    oc_runtime::construct(alg, make_router_app());

    // chord() holds button 3 and button 4 down simultaneously. The runtime's
    // last_controls snapshot carries BOTH bits; this is the .mask the per-app
    // TU stamps onto every event so vendor hold-as-shift gestures read it.
    oc_ui_sim::chord(alg, { kNT_button3, kNT_button4 });

    const uint16_t mask = oc_runtime::last_controls_of(alg);
    REQUIRE((mask & kNT_button3) != 0);
    REQUIRE((mask & kNT_button4) != 0);
}

TEST_CASE("router: button matrix maps NT control bits to OC UiControl values", "[oc_router][mapping]") {
    int count = 0;
    const oc_runtime::ControlMapping* tbl = oc_runtime::button_mapping_table(count);

    REQUIRE(count == 4);

    auto oc_for = [&](uint16_t nt_bit) -> int {
        for (int i = 0; i < count; ++i) {
            if (tbl[i].nt_bit == nt_bit) return tbl[i].oc_control;
        }
        return -1;
    };

    REQUIRE(oc_for(kNT_button3)        == OC::CONTROL_BUTTON_UP);
    REQUIRE(oc_for(kNT_button4)        == OC::CONTROL_BUTTON_DOWN);
    REQUIRE(oc_for(kNT_encoderButtonL) == OC::CONTROL_BUTTON_L);
    REQUIRE(oc_for(kNT_encoderButtonR) == OC::CONTROL_BUTTON_R);
}
