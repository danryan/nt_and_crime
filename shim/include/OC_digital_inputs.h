#pragma once
// Define the vendor include guard so a quote-include of "OC_digital_inputs.h"
// from inside a vendor app header (e.g. APP_LORENZ.h:31, which resolves to its
// vendor sibling first, not through -Ishim/include) becomes a no-op once this
// shim shadow has been included ahead of the vendor app header. Without this,
// the vendor header would re-include and pull vendor OC_core.h / OC_gpio.h /
// OC_debug.h and the Teensy GPIO machinery, none of which the shim satisfies.
#ifndef OC_DIGITAL_INPUTS_H_
#define OC_DIGITAL_INPUTS_H_
#endif
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

// Per-input mask constants. Vendor OC_digital_inputs.h:20-25 defines
// DIGITAL_INPUT_MASK(x) = (0x1 << x) and the four named masks from it. Vendor
// apps (Harrington 1200, APP_H1200.h:482-488) reference the named masks
// directly to test the bitmask returned by clocked(). The shim's clocked()
// mask uses the same (0x1u << input) bit layout, so a clocked() result ANDed
// with DIGITAL_INPUT_n_MASK reports an edge on input n exactly as on hardware.
static constexpr uint32_t DIGITAL_INPUT_1_MASK = 0x1u << DIGITAL_INPUT_1;
static constexpr uint32_t DIGITAL_INPUT_2_MASK = 0x1u << DIGITAL_INPUT_2;
static constexpr uint32_t DIGITAL_INPUT_3_MASK = 0x1u << DIGITAL_INPUT_3;
static constexpr uint32_t DIGITAL_INPUT_4_MASK = 0x1u << DIGITAL_INPUT_4;

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
