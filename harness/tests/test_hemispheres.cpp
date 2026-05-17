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
using Catch::Approx;

namespace {

struct CalcSetup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
    hem_shim::HemispheresInstance* hi;
};

CalcSetup setup_calculate_left() {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    select_applet(alg, LEFT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
    step_n_frames(loaded, alg, bus, 32);  // triggers swap + Start

    return { loaded, alg, bus, as_instance(alg) };
}

void calculate_set_op(_NT_algorithm* alg, int op_left, int op_right) {
    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_calculate(op_left, op_right));
}

}  // namespace

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
    auto s = setup_calculate_left();
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 0);  // MIN_FN
    REQUIRE(((packed >> 8) & 0xFF) == 1);  // MAX_FN
}

TEST_CASE("calculate C13: serialise round-trip", "[calculate]") {
    auto s = setup_calculate_left();
    get_applet(s.hi, LEFT)->OnDataReceive(pack_calculate(5, 7));
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 5);
    REQUIRE(((packed >> 8) & 0xFF) == 7);
}

TEST_CASE("calculate C2: MIN selects lesser of In(0), In(1)", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.alg, 0, 0);  // MIN both channels

    std::memset(s.bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C3: MAX selects greater of In(0), In(1)", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.alg, 1, 1);  // MAX both channels

    std::memset(s.bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(4.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(4.0f).margin(0.01f));
}

TEST_CASE("calculate C4: SUM clamps at HEMISPHERE_MAX_CV", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.alg, 2, 2);  // SUM both channels

    std::memset(s.bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
    set_cv(s.bus, LEFT, 0, 6.0f, 8);   // = HEMISPHERE_MAX_CV (6V)
    set_cv(s.bus, LEFT, 1, 0.5f, 8);   // would push past max
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(6.0f).margin(0.01f));
}

TEST_CASE("calculate C5: SUM clamps at HEMISPHERE_MIN_CV", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.alg, 2, 2);

    std::memset(s.bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
    set_cv(s.bus, LEFT, 0, -6.0f, 8);  // = HEMISPHERE_MIN_CV (-6V)
    set_cv(s.bus, LEFT, 1, -0.5f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(-6.0f).margin(0.01f));
}
