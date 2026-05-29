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

TEST_CASE("scope_push decimates and freezes when full", "[verifier][scope]") {
    float buf[kScopeWidth];
    int wr = 0, phase = 0; bool filled = false;
    // 512 samples at decim=2 -> exactly 256 kept, then frozen.
    float chunk[64];
    for (int i = 0; i < 64; ++i) chunk[i] = (float)i;
    int pushed = 0;
    while (!filled && pushed < 16) { scope_push(buf, wr, phase, filled, chunk, 64, 2); ++pushed; }
    REQUIRE(filled);
    REQUIRE(wr == kScopeWidth);
    // first kept sample is chunk[0]=0, decim=2 keeps even indices
    REQUIRE(buf[0] == Catch::Approx(0.0f));
    REQUIRE(buf[1] == Catch::Approx(2.0f));
}

TEST_CASE("scope_trigger finds first rising zero-cross", "[verifier][scope]") {
    float buf[8] = {-1, -0.5f, 0.5f, 1, 0.5f, -0.5f, -1, 0.2f};
    REQUIRE(scope_trigger(buf, 8) == 2);   // buf[1]<0, buf[2]>=0
}

TEST_CASE("scope_trigger falls back to 0 when no crossing", "[verifier][scope]") {
    float buf[4] = {1, 1, 1, 1};   // DC, no rising zero-cross
    REQUIRE(scope_trigger(buf, 4) == 0);
}
