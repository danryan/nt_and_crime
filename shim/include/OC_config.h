#pragma once
#include <cstdint>
#include <cstddef>

// Minimal net-new shim OC_config.h. The shim shadows OC_digital_inputs.h (the
// header that pulls vendor OC_config.h), so this header exists only to expose
// the two constants the O_C apps reach for. The vendor OC_config.h also carries
// a Teensy pin map and EEPROM layout; none of that is reproduced.
//
// Deliberate non-collision: vendor OC_config.h defines DAC_CHANNEL_COUNT and
// ADC_CHANNEL_COUNT. The shim OWNS those in OC_DAC.h (DAC_CHANNEL_COUNT) and
// OC_ADC.h (ADC_CHANNEL_COUNT). A future O_C-app TU includes OC_config.h
// alongside both, so this header must NOT redefine either; a duplicate
// `static constexpr` definition is a hard compile error.

// The vendor ISR cadence (vendor OC_config.h:23, global scope). The O_C
// runtime's tick accumulator holds this 16.666 kHz average against the NT
// sample rate.
static constexpr uint32_t OC_CORE_ISR_FREQ = 16666U;

namespace OC {
// Harrington 1200's TriggerDelays bound (vendor OC_config.h:38, namespace OC).
static constexpr size_t kMaxTriggerDelayTicks = 96;
}  // namespace OC
