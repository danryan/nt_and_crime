#include "catch.hpp"
#include "OC_strings.h"

TEST_CASE("OC_strings vendor tables are accessible", "[oc_strings]") {
    // cv_input_names_none: extern const char * const cv_input_names_none[]
    REQUIRE(OC::Strings::cv_input_names_none[0] != nullptr);

    // trigger_delay_times: extern const char * const trigger_delay_times[kNumDelayTimes]
    REQUIRE(OC::Strings::trigger_delay_times[0] != nullptr);

    // kNumDelayTimes: static const int kNumDelayTimes = 8
    REQUIRE(OC::kNumDelayTimes == 8);

    // trigger_delay_ticks: extern const uint8_t trigger_delay_ticks[]
    REQUIRE(OC::trigger_delay_ticks[1] == 2);  // [0]=0, [1]=2, [7]=66
}
