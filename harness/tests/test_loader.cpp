#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>

TEST_CASE("loader can resolve a plugin's factory and call construct", "[loader]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    REQUIRE(loaded->factory->guid != 0);
}
