#include "catch.hpp"
#include "nt_runtime.h"

TEST_CASE("bus frame layout matches api.h convention", "[runtime]") {
    nt::reset_runtime();
    REQUIRE(nt::num_buses() == 64);
    REQUIRE(nt::bus_frame_count() > 0);

    float* in1 = nt::bus_pointer(1, 32);
    float* in2 = nt::bus_pointer(2, 32);
    REQUIRE(in2 - in1 == 32);  // contiguous, bus 1 then bus 2

    *in1 = 0.5f;
    REQUIRE(nt::bus_pointer(1, 32)[0] == Catch::Approx(0.5f));
}

TEST_CASE("bus 0 is the unmapped sentinel", "[runtime]") {
    REQUIRE(nt::bus_pointer(0, 32) == nullptr);
}
