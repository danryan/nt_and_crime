#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>
#include <cstring>
#include <cmath>

// Applet selector indices (mirrors hem_shim::AppletIndex in
// shim/include/HemispheresFactory.h). Hard-coded here because that header
// transitively includes the upstream Hemispheres applet zoo, whose .h files
// define non-inline free functions and would clash at link time with the
// strong copies in build/host/Hemispheres.host.o.
static constexpr int kAppletBrancher  = 2;
static constexpr int kAppletCalculate = 5;

TEST_CASE("hemispheres factory loads, steps, draws without crash", "[smoke]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);

    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    auto* alg = loaded->algorithm;
    const_cast<int16_t*>(alg->v)[0] = (int16_t)kAppletBrancher;
    const_cast<int16_t*>(alg->v)[1] = (int16_t)kAppletCalculate;

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    std::memset(bus, 0, sizeof(float) * 64 * 32);

    for (int i = 0; i < 10; ++i) {
        loaded->factory->step(alg, bus, 8);
    }

    REQUIRE(loaded->factory->draw(alg) == true);

    for (int b = 1; b <= 16; ++b) {
        const float* slice = bus + (b - 1) * 32;
        for (int f = 0; f < 32; ++f) {
            REQUIRE(std::isfinite(slice[f]));
        }
    }
}
