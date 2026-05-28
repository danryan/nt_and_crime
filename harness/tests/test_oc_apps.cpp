#include "catch.hpp"
#include "OC_apps.h"
#include "OC_ui.h"
#include "Arduino.h"

// Dummy function pointers for testing App struct
static void dummy_void_fn() {}
static size_t dummy_size_fn() { return 0; }
static size_t dummy_save_fn(void *) { return 0; }
static size_t dummy_restore_fn(const void *) { return 0; }
static void dummy_event_fn(OC::AppEvent) {}
static void dummy_ui_event_fn(const OC::UI::Event &) {}

TEST_CASE("OC::App struct has correct fields", "[oc_apps]") {
  // Test that we can construct an App aggregate with proper function pointers
  OC::App app = {
    /* id */ 0x1234,
    /* name */ "Test App",
    /* Init */ dummy_void_fn,
    /* storageSize */ dummy_size_fn,
    /* Save */ dummy_save_fn,
    /* Restore */ dummy_restore_fn,
    /* HandleAppEvent */ dummy_event_fn,
    /* loop */ dummy_void_fn,
    /* DrawMenu */ dummy_void_fn,
    /* DrawScreensaver */ dummy_void_fn,
    /* HandleButtonEvent */ dummy_ui_event_fn,
    /* HandleEncoderEvent */ dummy_ui_event_fn,
    /* isr */ dummy_void_fn,
  };

  REQUIRE(app.id == 0x1234);
  REQUIRE(app.name != nullptr);
}

TEST_CASE("OC::AppEvent enum values are correct", "[oc_apps]") {
  REQUIRE(OC::APP_EVENT_SUSPEND == 0);
  REQUIRE(OC::APP_EVENT_RESUME == 1);
  REQUIRE(OC::APP_EVENT_SCREENSAVER_ON == 2);
  REQUIRE(OC::APP_EVENT_SCREENSAVER_OFF == 3);
}

TEST_CASE("OC::UiControl enum values are correct", "[oc_ui]") {
  REQUIRE(OC::CONTROL_BUTTON_UP == 1);
  REQUIRE(OC::CONTROL_BUTTON_DOWN == 2);
  REQUIRE(OC::CONTROL_BUTTON_L == 4);
  REQUIRE(OC::CONTROL_BUTTON_R == 8);
  REQUIRE(OC::CONTROL_BUTTON_M == 16);
  REQUIRE(OC::CONTROL_BUTTON_UP2 == 32);
  REQUIRE(OC::CONTROL_BUTTON_DOWN2 == 64);
  REQUIRE(OC::CONTROL_ENCODER_L == 256);
  REQUIRE(OC::CONTROL_ENCODER_R == 512);
  REQUIRE(OC::CONTROL_BUTTON_A == OC::CONTROL_BUTTON_UP);
  REQUIRE(OC::CONTROL_BUTTON_B == OC::CONTROL_BUTTON_DOWN);
}

// Helper function with FASTRUN attribute to test it is a valid (no-op) define
FASTRUN static void test_fastrun_function() {}

TEST_CASE("FASTRUN define exists and is inert", "[fastrun]") {
  // If FASTRUN expands to nothing, the above function compiled without error
  // Calling it here verifies the function exists and is callable
  test_fastrun_function();
  REQUIRE(true);
}
