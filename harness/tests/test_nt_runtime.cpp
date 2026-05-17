#include "catch.hpp"
#include "nt_runtime.h"
#include <distingnt/api.h>

TEST_CASE("NT_screen is exactly 128*64 bytes and initially zero", "[runtime]") {
    REQUIRE(sizeof(NT_screen) == 128 * 64);
    nt::reset_runtime();
    for (size_t i = 0; i < 128 * 64; ++i) {
        REQUIRE(NT_screen[i] == 0);
    }
}

TEST_CASE("NT_globals is initialised with sane defaults", "[runtime]") {
    nt::reset_runtime();
    REQUIRE(NT_globals.sampleRate == 48000u);
    REQUIRE(NT_globals.maxFramesPerStep == 64u);
    REQUIRE(NT_globals.workBuffer != nullptr);
    REQUIRE(NT_globals.workBufferSizeBytes >= 64u * 1024u);
}
