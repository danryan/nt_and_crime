#pragma once

#include <cstdint>

namespace OC {

enum UiControl : uint16_t {
  CONTROL_BUTTON_UP   = 1 << 0,
  CONTROL_BUTTON_DOWN = 1 << 1,
  CONTROL_BUTTON_L    = 1 << 2,
  CONTROL_BUTTON_R    = 1 << 3,
  CONTROL_BUTTON_M    = 1 << 4,
  CONTROL_BUTTON_UP2  = 1 << 5,
  CONTROL_BUTTON_DOWN2 = 1 << 6,

  CONTROL_ENCODER_L   = 1 << 8,
  CONTROL_ENCODER_R   = 1 << 9,
};

// Runtime aliases for UI remapping
const UiControl CONTROL_BUTTON_A = CONTROL_BUTTON_UP;
const UiControl CONTROL_BUTTON_B = CONTROL_BUTTON_DOWN;

// Minimal stand-in for the vendor OC::ui object. On hardware OC_ui.cpp owns a
// global Ui that, among much else, toggles encoder acceleration. Vendor apps
// call ui.encoder_enable_acceleration(control, enabled) from their app-event
// handlers (Automatonnetz disables left-encoder acceleration on RESUME). The
// shim drives encoders through the per-app customUi router with no acceleration
// model, so this is a no-op. Defined inline so no globals.cpp entry is needed.
struct UiOps {
  void encoder_enable_acceleration(UiControl /*control*/, bool /*enabled*/) {}
};
inline UiOps ui;

} // namespace OC
