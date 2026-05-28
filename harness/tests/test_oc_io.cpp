// O_C apps foundation: shim DAC accessor behavior.
//
// The load-bearing assertion is that the templated DAC accessor
// `OC::DAC::set<DAC_CHANNEL_A>(v)` compiles at all. Vendor O_C apps
// (APP_LORENZ.h:238) call `OC::DAC::set<DAC_CHANNEL_A>()`, where the
// template parameter takes the channel by reference
// (`template <DAC_CHANNEL &channel>`). An enum constant cannot bind to a
// `DAC_CHANNEL &`, so this file fails to COMPILE against the old
// `enum DAC_CHANNEL` representation. That compile failure is the RED test;
// the GREEN state switches the shim to the vendor `using = int` plus
// extern-object channel representation.
//
// DAC_CHANNEL_A..D are extern objects (defined in shim/src/globals.cpp);
// DAC_CHANNEL_LAST stays a compile-time constant for array bounds.
#include <cstdint>

#include "OC_DAC.h"
#include "OC_ADC.h"
#include "OC_digital_inputs.h"
#include "OC_config.h"

#include "catch.hpp"

TEST_CASE("templated set<channel> round-trips through value(index)", "[oc_io][dac]") {
    // Compile proof: the template parameter binds to the extern channel
    // object DAC_CHANNEL_A (an lvalue), not an enum constant.
    OC::DAC::set<DAC_CHANNEL_A>(1234);
    REQUIRE(OC::DAC::value(0) == 1234u);

    OC::DAC::set<DAC_CHANNEL_C>(40000);
    REQUIRE(OC::DAC::value(2) == 40000u);
}

TEST_CASE("runtime set(channel, value) round-trips through value(index)", "[oc_io][dac]") {
    OC::DAC::set(DAC_CHANNEL_B, 4096);
    REQUIRE(OC::DAC::value(1) == 4096u);
}

TEST_CASE("getHistory returns the last kHistoryDepth pushes in order", "[oc_io][dac]") {
    // Push kHistoryDepth + 2 distinct values; only the most recent
    // kHistoryDepth survive, oldest-to-newest.
    const uint16_t count = OC::DAC::kHistoryDepth + 2;
    for (uint16_t i = 0; i < count; ++i) {
        OC::DAC::set(DAC_CHANNEL_A, static_cast<int>(100 + i));
    }

    uint16_t history[OC::DAC::kHistoryDepth] = { 0 };
    OC::DAC::getHistory(DAC_CHANNEL_A, history);

    const uint16_t first_surviving = 100 + (count - OC::DAC::kHistoryDepth);
    for (uint16_t i = 0; i < OC::DAC::kHistoryDepth; ++i) {
        REQUIRE(history[i] == static_cast<uint16_t>(first_surviving + i));
    }
}

TEST_CASE("get_voltage_scaling collapses to 1V/oct", "[oc_io][dac]") {
    REQUIRE(OC::DAC::get_voltage_scaling(DAC_CHANNEL_A) == OC::VOLTAGE_SCALING_1V_PER_OCT);
}

TEST_CASE("templated ADC value<channel> reads the injected input", "[oc_io][adc]") {
    // Compile proof: the template parameter binds to the extern channel
    // object ADC_CHANNEL_1 (an lvalue), mirroring the vendor
    // `template <ADC_CHANNEL &channel>` signature (APP_LORENZ.h:191).
    oc_io::set_input(ADC_CHANNEL_1, 1536);
    REQUIRE(OC::ADC::value<ADC_CHANNEL_1>() == 1536);
}

TEST_CASE("runtime ADC value(channel) reads the injected input", "[oc_io][adc]") {
    oc_io::set_input(ADC_CHANNEL_2, -512);
    REQUIRE(OC::ADC::value(ADC_CHANNEL_2) == -512);

    oc_io::set_input(ADC_CHANNEL_4, 4096);
    REQUIRE(OC::ADC::value(ADC_CHANNEL_4) == 4096);
}

TEST_CASE("ADC pitch_value applies 1V/oct (12 semitones) scaling", "[oc_io][adc]") {
    // The NT input bus is already in 1V/oct hem units (12 << 7 = 1536 per
    // octave, 128 per semitone), so one octave of input reads back as one
    // octave of pitch and one semitone reads back as 128.
    oc_io::set_input(ADC_CHANNEL_1, 1536);
    REQUIRE(OC::ADC::pitch_value(ADC_CHANNEL_1) == 1536);

    oc_io::set_input(ADC_CHANNEL_1, 128);
    REQUIRE(OC::ADC::pitch_value(ADC_CHANNEL_1) == 128);
}

TEST_CASE("ADC raw_value and raw_pitch_value are present", "[oc_io][adc]") {
    oc_io::set_input(ADC_CHANNEL_3, 768);
    REQUIRE(OC::ADC::raw_value(ADC_CHANNEL_3) == 768);
    REQUIRE(OC::ADC::raw_pitch_value(ADC_CHANNEL_3) == 768);
}

TEST_CASE("DigitalInputs::clocked reports a rising edge then clears after Scan", "[oc_io][di]") {
    // Establish a known-low baseline so the first edge is unambiguous.
    oc_io::set_trigger(OC::DIGITAL_INPUT_1, false);
    OC::DigitalInputs::Scan();
    REQUIRE(OC::DigitalInputs::clocked(OC::DIGITAL_INPUT_1) == 0u);

    // Rising edge: low -> high. Scan latches the edge.
    oc_io::set_trigger(OC::DIGITAL_INPUT_1, true);
    OC::DigitalInputs::Scan();
    REQUIRE(OC::DigitalInputs::clocked(OC::DIGITAL_INPUT_1) != 0u);
    REQUIRE(OC::DigitalInputs::clocked<OC::DIGITAL_INPUT_1>() != 0u);

    // Held high (no new edge): the next Scan clears the clocked flag.
    OC::DigitalInputs::Scan();
    REQUIRE(OC::DigitalInputs::clocked(OC::DIGITAL_INPUT_1) == 0u);
}

TEST_CASE("DigitalInputs edge detection is per-input independent", "[oc_io][di]") {
    oc_io::set_trigger(OC::DIGITAL_INPUT_2, false);
    oc_io::set_trigger(OC::DIGITAL_INPUT_3, false);
    OC::DigitalInputs::Scan();

    oc_io::set_trigger(OC::DIGITAL_INPUT_2, true);
    OC::DigitalInputs::Scan();
    REQUIRE(OC::DigitalInputs::clocked(OC::DIGITAL_INPUT_2) != 0u);
    REQUIRE(OC::DigitalInputs::clocked(OC::DIGITAL_INPUT_3) == 0u);
}

TEST_CASE("DigitalInputs::read_immediate reports the live trigger level", "[oc_io][di]") {
    oc_io::set_trigger(OC::DIGITAL_INPUT_4, true);
    REQUIRE(OC::DigitalInputs::read_immediate(OC::DIGITAL_INPUT_4) == true);
    REQUIRE(OC::DigitalInputs::read_immediate<OC::DIGITAL_INPUT_4>() == true);

    oc_io::set_trigger(OC::DIGITAL_INPUT_4, false);
    REQUIRE(OC::DigitalInputs::read_immediate(OC::DIGITAL_INPUT_4) == false);
}

TEST_CASE("OC_config exposes the vendor isr freq and trigger-delay bound", "[oc_io][config]") {
    REQUIRE(OC_CORE_ISR_FREQ == 16666u);
    REQUIRE(OC::kMaxTriggerDelayTicks == 96u);
}
