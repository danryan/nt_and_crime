#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "applet_indices.h"
#include "applet_test_helpers.h"
#include "HemisphereApplet.h"
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

TEST_CASE("calculate C1: Start defaults are MIN, MAX", "[calculate]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, hem_shim::kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
    step_n_frames(loaded, alg, bus, 32);  // triggers swap + Start

    auto* hi = as_instance(alg);
    uint64_t packed = get_applet(hi, LEFT)->OnDataRequest();
    int op0 = (int)(packed & 0xFF);
    int op1 = (int)((packed >> 8) & 0xFF);
    REQUIRE(op0 == 0);  // MIN_FN
    REQUIRE(op1 == 1);  // MAX_FN
}

TEST_CASE("calculate C13: serialise round-trip", "[calculate]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, hem_shim::kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_calculate(5, 7));
    uint64_t packed = get_applet(hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 5);
    REQUIRE(((packed >> 8) & 0xFF) == 7);
}
