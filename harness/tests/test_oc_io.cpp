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
