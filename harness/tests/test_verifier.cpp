#include "catch.hpp"
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
