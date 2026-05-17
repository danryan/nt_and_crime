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
using Catch::Approx;
using namespace hem_test;

namespace {

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

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
    clear_bus(bus);
    step_n_frames(loaded, alg, bus, 32);  // triggers swap + Start

    return { loaded, alg, bus, as_instance(alg) };
}

void calculate_set_op(hem_shim::HemispheresInstance* hi, int op_left, int op_right) {
    get_applet(hi, LEFT)->OnDataReceive(pack_calculate(op_left, op_right));
}

struct BranchSetup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
    hem_shim::HemispheresInstance* hi;
};

BranchSetup setup_brancher_left() {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    select_applet(alg, LEFT, kAppletBrancher);

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    step_n_frames(loaded, alg, bus, 32);  // triggers swap + Start

    return { loaded, alg, bus, as_instance(alg) };
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
    clear_bus(bus);

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
    calculate_set_op(s.hi, 0, 0);  // MIN both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C3: MAX selects greater of In(0), In(1)", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 1, 1);  // MAX both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(4.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(4.0f).margin(0.01f));
}

TEST_CASE("calculate C4: SUM clamps at HEMISPHERE_MAX_CV", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 2, 2);  // SUM both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 6.0f, 8);   // = HEMISPHERE_MAX_CV (6V)
    set_cv(s.bus, LEFT, 1, 0.5f, 8);   // would push past max
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(6.0f).margin(0.01f));
}

TEST_CASE("calculate C5: SUM clamps at HEMISPHERE_MIN_CV", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 2, 2);  // SUM both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, -6.0f, 8);  // = HEMISPHERE_MIN_CV (-6V)
    set_cv(s.bus, LEFT, 1, -0.5f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(-6.0f).margin(0.01f));
}

TEST_CASE("calculate C6: DIFF returns absolute difference", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 3, 3);  // DIFF both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    set_cv(s.bus, LEFT, 1, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));

    // Swap inputs: DIFF is absolute, output stays 2V.
    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    set_cv(s.bus, LEFT, 1, 1.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C7: MEAN returns (a+b)/2", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 4, 4);  // MEAN both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    set_cv(s.bus, LEFT, 1, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C8: both channels read both inputs (asymmetric quirk)", "[calculate]") {
    // Vendor Calculate.h:82-84. result = calc_fn[op[ch]](In(0), In(1)).
    // In(0) and In(1) are shared across channels, NOT per-channel. So
    // op[0]=MIN + op[1]=MAX with In(0)=1V, In(1)=3V yields Out(0)=1V, Out(1)=3V.
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 0, 1);  // op[0]=MIN, op[1]=MAX

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    set_cv(s.bus, LEFT, 1, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(1.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.01f));
}

TEST_CASE("calculate C9: S&H output stays at zero before clock", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 5, 5);  // S&H both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);  // input CV present
    set_cv(s.bus, LEFT, 1, 3.0f, 8);
    // No gate written; Clock(ch) never fires; S&H never latches.
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(0.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("calculate C10: S&H captures input on clock edge", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 5, 5);  // S&H both channels

    // Step 1: rising edge on Gate(0) plus held CV.
    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_gate(s.bus, LEFT, 0, 0, 8);  // rising edge at frame 0
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // Subsequent steps: keep CV at 2V, no new gate edges. ADC lag is internal
    // to the vendor; stepping ~16 buffers (512 samples) is safely past it.
    for (int i = 0; i < 16; ++i) {
        clear_bus(s.bus);
        set_cv(s.bus, LEFT, 0, 2.0f, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.1f));
}

TEST_CASE("calculate C11: Rnd+ outputs in [0, HEMISPHERE_MAX_CV)", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 6, 6);  // Rnd+ both channels
    seed_hem_rng(0xDEADBEEF);

    bool saw_nonzero = false;
    for (int i = 0; i < 100; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        float v = read_cv_at(s.bus, LEFT, 0, 0, 8);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <  6.001f);   // HEMISPHERE_MAX_CV = 6V plus tiny margin
        if (v > 0.01f) saw_nonzero = true;
    }
    REQUIRE(saw_nonzero);  // at least one nonzero roll out of 100
}

TEST_CASE("calculate C12: Rnd+ latches to clocked after first Clock(0)", "[calculate]") {
    auto s = setup_calculate_left();
    calculate_set_op(s.hi, 6, 6);  // Rnd+ both channels
    seed_hem_rng(0xCAFEBABE);

    // Step 1: rising edge on Gate(0) latches Rnd+ to clocked mode.
    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    float v_after_clock = read_cv_at(s.bus, LEFT, 0, 0, 8);

    // Subsequent unclocked steps: output should hold steady.
    for (int i = 0; i < 5; ++i) {
        clear_bus(s.bus);
        // No gate edge written.
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(v_after_clock).margin(0.001f));
    }
}

TEST_CASE("brancher B1: Start sets p = 50", "[brancher]") {
    auto s = setup_brancher_left();
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0x7F) == 50);
}

TEST_CASE("brancher B9: serialise round-trip preserves p", "[brancher]") {
    auto s = setup_brancher_left();
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(73));
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0x7F) == 73);
}

TEST_CASE("brancher B2: p=100 always routes gate to output 0", "[brancher]") {
    auto s = setup_brancher_left();
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);

    // Sustained high gate across all 32 frames so Gate(0) reads true and
    // the rising-edge transition fires Clock(0) exactly once for this step.
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);
}

TEST_CASE("brancher B3: p=0 always routes gate to output 1", "[brancher]") {
    auto s = setup_brancher_left();
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(0));
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);
}

TEST_CASE("brancher B5: logical clock (no physical gate) emits ClockOut pulse", "[brancher]") {
    // Vendor: if (Clock(0)) { clocked = !Gate(0); if (clocked) ClockOut(choice); }
    // Pulse the gate input for one frame only so a rising edge fires but the
    // gate is not held high across the buffer. Gate(0) returns false because
    // last_high is reset before the next read_gate scan. ClockOut emits a brief
    // pulse (HEMISPHERE_CLOCK_TICKS = 175 ticks).
    auto s = setup_brancher_left();
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);

    // Single-sample pulse: rising edge fires Clock(0), but last_high resets
    // before next scan, so Gate(0) reads false. clocked = !Gate(0) = true,
    // and ClockOut(choice) fires.
    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // single-sample pulse at frame 0
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // With p=100, choice rolls to 0. Output 0 receives the ClockOut pulse.
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);
}
