#pragma once
#include <cstdint>
#include <cstddef>

// Include-guard poison (CLAUDE.md "Shadowing a vendor header quote-included
// from inside another vendor header"). A vendor app header pulled into a per-app
// TU quote-includes "OC_ADC.h" from inside the vendor tree (APP_FPART.h:43),
// which resolves to the vendor sibling, not this shim shadow. Defining the
// vendor guard (OC_ADC_H_) here makes that sibling self-suppress; this shim
// shadow already provides the OC::ADC accessors (raw_pitch_value, the channel
// objects) the apps use.
#ifndef OC_ADC_H_
#define OC_ADC_H_
#endif

// O_C-only shim ADC accessor. Net-new; the Hemisphere applets never include
// this header. Backed by the NT input bus, refreshed into the O_C input store
// (oc_io, defined in shim/src/oc/io.cpp) by the O_C runtime before each isr().
//
// Channel representation (load-bearing). Vendor OC_ADC.h:14-16 declares
// `using ADC_CHANNEL = int;` plus extern channel objects, and the templated
// accessor takes the channel by reference (`template <ADC_CHANNEL &channel>`,
// vendor OC_ADC.h:59). An enum constant cannot bind to an `ADC_CHANNEL &`, so
// the channel symbols are extern lvalue objects defined in shim/src/oc/io.cpp.
// This mirrors the DAC_CHANNEL representation in OC_DAC.h.
using ADC_CHANNEL = int;
extern ADC_CHANNEL ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4;

// Channel count / array bound. Vendor places this in OC_config.h, but the shim
// OC_config.h is deliberately minimal and must not collide with the DAC count
// owned by OC_DAC.h, so OC_ADC.h owns ADC_CHANNEL_COUNT. The shim never defines
// ARDUINO_TEENSY41 / __IMXRT1062__, so the four-channel form holds (Low-rents
// reads ADC_CHANNEL_1..4, not 5..8).
static constexpr int ADC_CHANNEL_COUNT = 4;
static constexpr int ADC_CHANNEL_LAST = ADC_CHANNEL_COUNT;

// Backing store for the O_C input bus. The O_C runtime pushes the routed NT
// inputs[] values here before each isr(); tests inject directly. This is
// O_C-only and lives in shim/src/oc/io.cpp, never in globals.cpp (which is
// aggregated into every Hemisphere applet).
namespace oc_io {
void set_input(int channel, int value);
int input(int channel);
}  // namespace oc_io

namespace OC {

class ADC {
public:
  // The raw NT input bus is already in 1V/oct hem units (12 << 7 = 1536 per
  // octave, 128 per semitone). pitch_value therefore returns the input value
  // directly: the bus space IS the vendor pitch space. The shim has no separate
  // smoothed/raw ADC pipeline (vendor OC_ADC.h:91 DMA smoothing), so raw_value
  // returns the same routed value as value().

  template <ADC_CHANNEL &channel>
  static int32_t value() {
    return oc_io::input(channel);
  }

  static int32_t value(ADC_CHANNEL channel) {
    return oc_io::input(channel);
  }

  static uint32_t raw_value(ADC_CHANNEL channel) {
    return static_cast<uint32_t>(oc_io::input(channel));
  }

  static int32_t pitch_value(ADC_CHANNEL channel) {
    return value(channel);
  }

  // Harrington 1200 uses raw_pitch_value (APP_H1200.h:714). With no separate
  // raw pipeline it collapses to pitch_value.
  static int32_t raw_pitch_value(ADC_CHANNEL channel) {
    return pitch_value(channel);
  }
};

}  // namespace OC
