#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "applet_indices.h"
#include "applet_test_helpers.h"
#include <distingnt/api.h>
#include <cstring>
#include <cmath>

using hem_shim::kAppletBrancher;
using hem_shim::kAppletCalculate;
using namespace hem_test;

TEST_CASE("hemispheres factory loads, steps, draws without crash", "[smoke]") {
    nt::reset_runtime();

    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    auto* alg = loaded->algorithm;
    select_applet(alg, LEFT,  kAppletBrancher);
    select_applet(alg, RIGHT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());

    int advanced = step_n_frames(loaded, alg, bus, 320);
    REQUIRE(advanced == 320);

    REQUIRE(loaded->factory->draw(alg) == true);

    for (int b = 1; b <= 16; ++b) {
        const float* slice = bus + (b - 1) * nt::bus_frame_count();
        for (int f = 0; f < nt::bus_frame_count(); ++f) {
            REQUIRE(std::isfinite(slice[f]));
        }
    }
}
