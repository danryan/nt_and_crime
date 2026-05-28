#pragma once
#include <cstdint>

// O_C-only shim digital-input accessor. Net-new; shadows vendor
// OC_digital_inputs.h (which pulls vendor OC_config.h / OC_gpio.h and the
// Teensy GPIO ISR machinery). Backed by the O_C trigger store (oc_io, defined
// in shim/src/oc/io.cpp). The O_C runtime drives the trigger levels from the
// routed NT trigger buses and calls Scan() once per isr() tick, matching the
// Hemisphere rising-edge discipline so one bus edge is seen by exactly one tick.

namespace OC {

// Vendor OC_digital_inputs.h:11-18. An enum is fine here: the vendor templates
// take the input by value (`template <DigitalInput input>`), not by reference.
enum DigitalInput {
  DIGITAL_INPUT_1,
  DIGITAL_INPUT_2,
  DIGITAL_INPUT_3,
  DIGITAL_INPUT_4,
  DIGITAL_INPUT_LAST
};

}  // namespace OC

// Backing store for the O_C trigger bus. set_trigger sets the live level;
// Scan() latches low->high transitions into the clocked mask. Tests inject via
// set_trigger directly; the O_C runtime sets the level from the routed NT
// trigger buses. O_C-only; lives in shim/src/oc/io.cpp.
namespace oc_io {
void set_trigger(int input, bool high);
bool trigger(int input);
void scan_triggers();
uint32_t clocked_mask();
}  // namespace oc_io

namespace OC {

class DigitalInputs {
public:
  static void Init() {}
  static void reInit() { Init(); }

  // Latch low->high transitions on every trigger into the clocked mask.
  static void Scan() { oc_io::scan_triggers(); }

  // Mask of all inputs that saw a rising edge at the last Scan().
  static inline uint32_t clocked() {
    return oc_io::clocked_mask();
  }

  template <DigitalInput input> static inline uint32_t clocked() {
    return oc_io::clocked_mask() & (0x1u << input);
  }

  static inline uint32_t clocked(DigitalInput input) {
    return oc_io::clocked_mask() & (0x1u << input);
  }

  // Live trigger level, independent of edge latching.
  template <DigitalInput input> static inline bool read_immediate() {
    return oc_io::trigger(input);
  }

  static inline bool read_immediate(DigitalInput input) {
    return oc_io::trigger(input);
  }
};

}  // namespace OC
