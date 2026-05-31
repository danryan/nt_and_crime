#pragma once
// Phazerville T4.1 detection. NorthernLightModular variant uses bigger CV range;
// the NT shim is always the T4.1 layout, so this stays 0.
#define NorthernLightModular 0

// Trigger-input pin identifiers (1-based). Vendor OC_gpio.h binds TR1..TR4 to
// Teensy GPIO pins; the NT shim maps them to the four digital inputs. POLYLFO
// reads digitalReadFast(TR4) for the free-run frequency-multiplier override.
//
// digitalReadFast is defined inline here (not via a globals.cpp accessor) so the
// only TUs that pull oc_io::trigger via this path are the OC-app TUs that
// actually include OC_gpio.h. Hemisphere applet tests never include this header,
// so the OC-only oc_io backing (shim/src/oc/io.cpp) is not dragged into their
// link. OC_digital_inputs.h is self-contained (its own guard), so including it
// here is not a cycle. Vendor digitalReadFast(TR4) reads the raw TR4 gate pin;
// the NT analog is the immediate level of digital input (pin - 1).
#include "OC_digital_inputs.h"

enum { TR1 = 1, TR2 = 2, TR3 = 3, TR4 = 4 };

inline bool digitalReadFast(int pin) {
    return OC::DigitalInputs::read_immediate(
        static_cast<OC::DigitalInput>(pin - 1));
}
