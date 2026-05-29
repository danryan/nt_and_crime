#include "catch.hpp"
#include <string>
#include <cstring>
#include "../../plugins/probes/verifier_logic.h"

using namespace verifier;

TEST_CASE("reduction accumulates mean min max pkpk", "[verifier][reduce]") {
    Reduction r;
    reduction_reset(r);
    const float a[] = {1.0f, -1.0f, 0.5f};
    reduction_accumulate(r, a, 3);
    REQUIRE(reduction_value(r, kMin)  == Catch::Approx(-1.0f));
    REQUIRE(reduction_value(r, kMax)  == Catch::Approx(1.0f));
    REQUIRE(reduction_value(r, kPkPk) == Catch::Approx(2.0f));
    REQUIRE(reduction_value(r, kMean) == Catch::Approx(0.5f / 3.0f * 1.0f).margin(0.0001));
}

TEST_CASE("reduction spans multiple accumulate calls", "[verifier][reduce]") {
    Reduction r;
    reduction_reset(r);
    const float a[] = {2.0f, 2.0f};
    const float b[] = {4.0f, 4.0f};
    reduction_accumulate(r, a, 2);
    reduction_accumulate(r, b, 2);
    REQUIRE(reduction_value(r, kMean) == Catch::Approx(3.0f));
    REQUIRE(reduction_value(r, kMax)  == Catch::Approx(4.0f));
}

TEST_CASE("empty reduction reads zero", "[verifier][reduce]") {
    Reduction r;
    reduction_reset(r);
    REQUIRE(reduction_value(r, kMean) == Catch::Approx(0.0f));
}

TEST_CASE("volts_to_millivolts rounds away from zero", "[verifier][fmt]") {
    REQUIRE(volts_to_millivolts(1.2345f) == 1235);   // .2345 -> rounds 1234.5 up
    REQUIRE(volts_to_millivolts(-1.2345f) == -1235);
    REQUIRE(volts_to_millivolts(0.0f) == 0);
}

TEST_CASE("format_mv fixed width sNN.fff with leading zeros", "[verifier][fmt]") {
    char b[8];
    format_mv(1000, b);  REQUIRE(std::string(b) == "+01.000");
    format_mv(-250, b);  REQUIRE(std::string(b) == "-00.250");
    format_mv(0, b);     REQUIRE(std::string(b) == "+00.000");
    format_mv(99999, b); REQUIRE(std::string(b) == "+99.999");
}

TEST_CASE("format_mv flags overflow with sentinel", "[verifier][fmt]") {
    char b[8];
    format_mv(100000, b);
    REQUIRE(b[0] == '+');
    REQUIRE(std::string(b).find('#') != std::string::npos);
}
