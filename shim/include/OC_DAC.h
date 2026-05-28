#pragma once
#include <cstdint>
#include <cstddef>

// Channel representation (load-bearing). Vendor OC_DAC.h:26-28 declares
// `using DAC_CHANNEL = int;` plus extern channel objects, and the templated
// accessors take the channel by reference (`template <DAC_CHANNEL &channel>`,
// vendor OC_DAC.h:102). An enum constant cannot bind to a `DAC_CHANNEL &`, so
// the channel symbols must be extern lvalue objects. This header previously
// declared `enum DAC_CHANNEL`; the O_C apps foundation switches it to the
// vendor form so `OC::DAC::set<DAC_CHANNEL_A>()` compiles. The objects are
// defined in shim/src/globals.cpp. The Hemisphere applets only ever use
// DAC_CHANNEL as a value type (function params, C-style casts of an int), so
// the change is value-compatible for them.
using DAC_CHANNEL = int;
extern DAC_CHANNEL DAC_CHANNEL_A, DAC_CHANNEL_B, DAC_CHANNEL_C, DAC_CHANNEL_D;

// Channel count / array bound. Stays a compile-time constant. Used as an
// array bound by the ported menu widgets (averaged_scope_history,
// OC_menus.cpp:101) and the shim DAC's own value/history arrays.
static constexpr int DAC_CHANNEL_COUNT = 4;
static constexpr int DAC_CHANNEL_LAST = DAC_CHANNEL_COUNT;

namespace OC {

// Vendor OC_DAC.h:36-46. The NT outputs are 1V/oct, so the shim collapses the
// alternate scalings: get_voltage_scaling always reports 1V/oct and
// set_voltage_scaled_semitone falls through to the standard semitone path. The
// full enum is kept so vendor call sites that name a scaling still compile.
enum OutputVoltageScaling {
    VOLTAGE_SCALING_1V_PER_OCT,    // 0
    VOLTAGE_SCALING_CARLOS_ALPHA,  // 1
    VOLTAGE_SCALING_CARLOS_BETA,   // 2
    VOLTAGE_SCALING_CARLOS_GAMMA,  // 3
    VOLTAGE_SCALING_BOHLEN_PIERCE, // 4
    VOLTAGE_SCALING_QUARTERTONE,   // 5
    VOLTAGE_SCALING_1_2V_PER_OCT,  // 6
    VOLTAGE_SCALING_2V_PER_OCT,    // 7
    VOLTAGE_SCALING_LAST
};

namespace DAC {

// Octave bias for MIDIQuantizer note-number conversion and pitch-to-dac. Vendor
// OC_DAC.h defines this on the target hardware as 5; tests and vendor
// MIDIQuantizer pass it as the default `bias` parameter.
static constexpr uint8_t kOctaveZero = 5;

// Output-history ring depth. Vendor OC_DAC.h:52. The Low-rents screensaver
// reaches getHistory via scope_averaging (OC_menus.cpp:116).
static constexpr size_t kHistoryDepth = 8;

// Pitch units per octave: 12 semitones * 128, matching the Hemisphere shim's
// ONE_OCTAVE (HSUtils.h:15). The shim is 1V/oct (DAC_20Vpp = 0), so the
// interval size is a plain 12 << 7. Output is in the same hem-unit space the
// NT outputs[] bus uses, so no calibration LUT is required.
static constexpr int kIntervalSize = 12 << 7;
static constexpr int kOctaves = 10;
static constexpr uint16_t kMaxValue = 65535;

// Unified 16-bit DAC code space (matches the vendor hardware DAC range). Every
// OC::DAC value is a 16-bit code: pitch apps reach it through pitch_to_dac,
// modulation apps (e.g. Lorenz) write full-scale codes directly. 0V sits at the
// code midpoint, and full scale 0..65535 spans kVoltsFullScale volts bipolar.
// The per-app runtime converts a code to NT bus volts with these constants, so
// pitch stays 1V/oct (one octave == kCodesPerVolt codes) while full-scale
// modulation outputs span +-kVoltsFullScale/2 around 0V. Replaces the earlier
// pitch-only model (value == pitch units, 1536/V), which railed full-scale codes.
static constexpr int   kDacZeroCode       = 32768;                         // 0V (= kOctaveZero)
static constexpr float kVoltsFullScale    = 10.0f;                         // 0..65535 -> -5V..+5V
static constexpr float kCodesPerVolt      = 65536.0f / kVoltsFullScale;    // 6553.6
static constexpr float kCodesPerPitchUnit = kCodesPerVolt / kIntervalSize; // 4.2667

namespace detail {
inline uint32_t (&values())[DAC_CHANNEL_COUNT] {
    static uint32_t values_[DAC_CHANNEL_COUNT] = { 0 };
    return values_;
}
inline uint16_t (&history())[DAC_CHANNEL_COUNT][kHistoryDepth] {
    static uint16_t history_[DAC_CHANNEL_COUNT][kHistoryDepth] = {{ 0 }};
    return history_;
}
inline size_t &history_tail() {
    static size_t history_tail_ = 0;
    return history_tail_;
}

inline uint32_t usat16(uint32_t v) {
    return v > kMaxValue ? kMaxValue : v;
}

// Push the latest output for one channel onto its history ring. The shim
// pushes on each set (the spec's deviation from vendor, which pushes in
// Update), because scope_averaging reads getHistory directly off the ring.
inline void push_history(int channel, uint32_t v) {
    const size_t tail = history_tail();
    history()[channel][tail] = static_cast<uint16_t>(v);
}
inline void advance_history_tail() {
    history_tail() = (history_tail() + 1) % kHistoryDepth;
}
}  // namespace detail

inline void set(DAC_CHANNEL channel, uint32_t value) {
    const uint32_t v = detail::usat16(value);
    detail::values()[channel] = v;
    detail::push_history(channel, v);
    detail::advance_history_tail();
}

template <DAC_CHANNEL &channel>
inline void set(uint32_t value) {
    set(channel, value);
}

inline uint32_t value(size_t index) {
    return detail::values()[index];
}

// Calculate DAC value from a pitch value (12*128 per octave). Mirrors vendor
// pitch_to_dac (OC_DAC.h:128) without the per-channel calibration LUT: the NT
// bus is linear 1V/oct, so the calibrated octave table collapses to
// pitch * (full-scale per octave). kOctaveZero biases 0V to C5.
inline int32_t pitch_to_dac(DAC_CHANNEL /*channel*/, int32_t pitch, int32_t octave_offset) {
    // Map pitch (kIntervalSize units/octave; pitch 0, octave 0 == note at
    // kOctaveZero == 0V) into the 16-bit code space: 0V at kDacZeroCode, one
    // octave == kCodesPerVolt codes. No per-channel calibration LUT (the NT bus
    // is linear 1V/oct). The route then converts the code back to volts with the
    // same constants, so 1V/oct is preserved.
    const float code = static_cast<float>(kDacZeroCode) +
        static_cast<float>(pitch + octave_offset * kIntervalSize) * kCodesPerPitchUnit;
    // Saturate at the rails (intentional): a request beyond +-5V from the 0V
    // reference cannot be represented by the 16-bit code or the bus, so it pins
    // to the nearest rail rather than wrapping. A NaN code (only reachable if a
    // caller passes a corrupt pitch whose float product overflows) fails both
    // comparisons and falls through; set() then backstops it via usat16, so that
    // redundant clamp is deliberate, not dead.
    if (code <= 0.0f) return 0;
    if (code >= static_cast<float>(kMaxValue)) return kMaxValue;
    return static_cast<int32_t>(code + 0.5f);
}

inline int32_t semitone_to_dac(DAC_CHANNEL channel, int32_t semi, int32_t octave_offset) {
    return pitch_to_dac(channel, semi << 7, octave_offset);
}

inline void set_pitch(DAC_CHANNEL channel, int32_t pitch, int32_t octave_offset) {
    set(channel, static_cast<uint32_t>(pitch_to_dac(channel, pitch, octave_offset)));
}

// Set integer voltage value, where 0 = 0V relative to kOctaveZero. Routes
// through pitch_to_dac so the stored value is a 16-bit code like every other
// DAC write.
inline void set_octave(DAC_CHANNEL channel, int v) {
    set(channel, static_cast<uint32_t>(pitch_to_dac(channel, 0, v)));
}

// 1V/oct collapse. Vendor returns the stored per-channel scaling; the NT bus
// has only one, so report it unconditionally.
inline uint8_t get_voltage_scaling(uint8_t /*channel_id*/) {
    return VOLTAGE_SCALING_1V_PER_OCT;
}

// Falls through to the standard semitone path: every alternate scaling is
// collapsed to 1V/oct, so voltage_scaling is ignored.
template <DAC_CHANNEL &channel>
inline void set_voltage_scaled_semitone(int32_t semitone, int32_t octave_offset, uint8_t /*voltage_scaling*/) {
    set<channel>(static_cast<uint32_t>(semitone_to_dac(channel, semitone, octave_offset)));
}

// Flush hook. The shim already pushes history on each set, so Update is a
// no-op kept for vendor call-site compatibility.
inline void Update() {}

// Copy the channel's history oldest-to-newest into dst[kHistoryDepth]. set()
// advances history_tail after every write, so the tail points at the slot the
// next write will overwrite, i.e. the oldest live entry. The chronological
// read therefore starts at tail and wraps.
inline void getHistory(int channel, uint16_t *dst) {
    const size_t head = detail::history_tail();
    const uint16_t (&ring)[kHistoryDepth] = detail::history()[channel];

    size_t count = kHistoryDepth - head;
    const uint16_t *src = ring + head;
    while (count--) *dst++ = *src++;

    count = head;
    src = ring;
    while (count--) *dst++ = *src++;
}

}  // namespace DAC
}  // namespace OC
