#pragma once
// CVInputMap shim — compat wrapper replacing the 20-LoC stub.
//
// Design: vendor CVInputMap.h (266 lines) is deeply coupled to vendor-only
// infrastructure (MIDIState, SemitoneQuantizer, CVMAP_MAX, OC::DIGITAL_INPUT_LAST,
// icon arrays, clock_m.IsRunning(), etc.) that the shim does not provide and
// has no use for. A verbatim port would require porting the entire vendor HSIOFrame
// and hundreds of lines of unneeded stubs.
//
// Instead this header provides a shim-native CVInputMap that:
//   1. Preserves backward compat: CVInputMap(int ch) constructor, In() returns
//      frame.inputs[ch] at default attenuversion=60 (Atten(60)=1000=100%).
//   2. Adopts vendor field layout (source, attenuversion) so vendor surface
//      methods (ChangeSource, Pack, Unpack) work for the applets that need them.
//   3. source = ch + 1 convention (vendor: 0=unmapped, 1..4=ADC 0..3).
//   4. bjorklund.h and clkdivmult.h are included here so all CVInputMap
//      consumers transitively get Euclidean and clock-div utilities.
//
// Call sites preserved verbatim:
//   HemisphereApplet.h:52  cvmap[ch + channel_offset()].In()
//   HemisphereApplet.h:61  trigmap[ch + channel_offset()].Gate()
#include <cstdint>
#include "HSIOFrame.h"
#include "cv_map/bjorklund.h"
// clkdivmult.h is NOT included here: vendor applets include it via relative
// path (../util/clkdivmult.h from their applet directory), which resolves to
// the vendor copy. Including our shim copy here would cause redefinition errors
// when both files reach the same translation unit. Applets or test code that
// need ClkDivMult should include "util/clkdivmult.h" explicitly.

// Number of ADC input channels provided by the shim (T4.1 layout).
static constexpr int CVMAP_ADC_LAST = 4;

// Atten: mirrors vendor util/util_math.h:55. Given a bipolar param value
// from -127..+128, returns a scalar in units of 0.1% (60 -> 1000 = 100%).
// Exponential curve: 10 * att * abs(att) / 36.
// Guarded so multiple dep headers can include this file without ODR errors.
#ifndef ATTEN_DEFINED
#define ATTEN_DEFINED
constexpr int Atten(int8_t att) {
    return 10 * att * (att < 0 ? -att : att) / 36;
}
#endif

struct CVInputMap {
    // source: 0 = unmapped (In() returns default_value).
    //         1..CVMAP_ADC_LAST = ADC channel (source - 1).
    // attenuversion: -127..+127; 60 = 100% passthrough (Atten(60) = 1000).
    int8_t source = 0;
    int8_t attenuversion = 60;

    // Backward-compat constructor used by globals.cpp.
    // Initialises source = ch + 1 so In() maps to frame.inputs[ch].
    explicit CVInputMap(int ch = -1) {
        if (ch >= 0) {
            source = static_cast<int8_t>(ch + 1);
        }
    }

    // Returns the scaled ADC input, or default_value when source == 0.
    // At attenuversion=60, Atten(60)=1000 so result == frame.inputs[source-1].
    int In(int default_value = 0) {
        if (!source) return default_value;
        int raw = HS::frame.inputs[source - 1];
        return raw * Atten(attenuversion) / 1000;
    }

    // Raw unscaled ADC input (no attenuversion). Returns default_value when
    // source == 0. Mirrors vendor CVInputMap::RawIn() for callers that need
    // the unscaled value.
    int RawIn(int default_value = 0) {
        if (!source) return default_value;
        return HS::frame.inputs[source - 1];
    }

    // Step source forward or backward by dir, clamped to [0, CVMAP_ADC_LAST].
    void ChangeSource(int dir) {
        int s = source + dir;
        if (s < 0) s = 0;
        if (s > CVMAP_ADC_LAST) s = CVMAP_ADC_LAST;
        source = static_cast<int8_t>(s);
    }

    // Serialise to 16 bits: low byte = source, high byte = attenuversion.
    uint16_t Pack() const {
        return static_cast<uint16_t>(source & 0xFF) |
               static_cast<uint16_t>(static_cast<uint8_t>(attenuversion) << 8);
    }

    // Deserialise from 16-bit Pack() output.
    void Unpack(uint16_t data) {
        int8_t s = static_cast<int8_t>(data & 0xFF);
        if (s < 0) s = 0;
        if (s > CVMAP_ADC_LAST) s = CVMAP_ADC_LAST;
        source = s;
        attenuversion = static_cast<int8_t>(data >> 8);
    }

    // Mirrors vendor CVInputMap::InputName at vendor CVInputMap.h:71. Returns
    // a short label for the current source. Used by Combin8::View for the
    // CV map editor display.
    const char* InputName() const {
        static const char* const names[CVMAP_ADC_LAST + 1] = {
            "--", "A", "B", "C", "D"
        };
        int s = source;
        if (s < 0) s = 0;
        if (s > CVMAP_ADC_LAST) s = CVMAP_ADC_LAST;
        return names[s];
    }
};

extern CVInputMap cvmap[4];

struct DigitalInputMap {
    // source: 0 = unmapped.  1..4 = gate_high[source-1].
    int8_t source = 0;

    // Backward-compat constructor used by globals.cpp.
    explicit DigitalInputMap(int ch = -1) {
        if (ch >= 0) {
            source = static_cast<int8_t>(ch + 1);
        }
    }

    // Returns gate state for the mapped channel, or false when unmapped.
    bool Gate() {
        if (!source) return false;
        return HS::frame.gate_high[source - 1];
    }

    // Step source forward or backward by dir, clamped to [0, CVMAP_ADC_LAST].
    void ChangeSource(int dir) {
        int s = source + dir;
        if (s < 0) s = 0;
        if (s > CVMAP_ADC_LAST) s = CVMAP_ADC_LAST;
        source = static_cast<int8_t>(s);
    }

    // Serialise to 16 bits: low byte = source.
    uint16_t Pack() const {
        return static_cast<uint16_t>(source & 0xFF);
    }

    // Deserialise from 16-bit Pack() output.
    void Unpack(uint16_t data) {
        int8_t s = static_cast<int8_t>(data & 0xFF);
        if (s < 0) s = 0;
        if (s > CVMAP_ADC_LAST) s = CVMAP_ADC_LAST;
        source = s;
    }
};

extern DigitalInputMap trigmap[4];
