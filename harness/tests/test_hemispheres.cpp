#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "applet_indices.h"
#include "applet_test_helpers.h"
#include "HemisphereApplet.h"
#include <distingnt/api.h>
#include <cstring>
#include <cmath>

using hem_shim::kAppletAttenuateOffset;
using hem_shim::kAppletBrancher;
using hem_shim::kAppletBurst;
using hem_shim::kAppletCalculate;
using hem_shim::kAppletCompare;
using hem_shim::kAppletLogic;
using hem_shim::kAppletSlew;
using Catch::Approx;
using namespace hem_test;

namespace {

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

struct AppletSetup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
    hem_shim::HemispheresInstance* hi;
};

AppletSetup setup_applet(hem_shim::AppletIndex idx, HemSide side = LEFT) {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    select_applet(alg, side, idx);

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    step_n_frames(loaded, alg, bus, 32);  // triggers swap + Start

    return { loaded, alg, bus, as_instance(alg) };
}

void calculate_set_op(hem_shim::HemispheresInstance* hi, int op_left, int op_right) {
    get_applet(hi, LEFT)->OnDataReceive(pack_calculate(op_left, op_right));
}

void logic_set_op(hem_shim::HemispheresInstance* hi, int op_left, int op_right) {
    get_applet(hi, LEFT)->OnDataReceive(pack_logic(op_left, op_right));
}

void atten_off_set(hem_shim::HemispheresInstance* hi,
                   int offset_left, int offset_right,
                   int level_left, int level_right,
                   bool mix = false) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_atten_off(offset_left, offset_right, level_left, level_right, mix));
}

void slew_set(hem_shim::HemispheresInstance* hi, int rise, int fall) {
    get_applet(hi, LEFT)->OnDataReceive(pack_slew(rise, fall));
}

void burst_set(hem_shim::HemispheresInstance* hi,
               int number, int spacing, int div, int jitter, int accel) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_burst(number, spacing, div, jitter, accel));
}

void compare_set(hem_shim::HemispheresInstance* hi, int level) {
    get_applet(hi, LEFT)->OnDataReceive(pack_compare(level));
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
    auto s = setup_applet(kAppletCalculate);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 0);  // MIN_FN
    REQUIRE(((packed >> 8) & 0xFF) == 1);  // MAX_FN
}

TEST_CASE("calculate C13: serialise round-trip", "[calculate]") {
    auto s = setup_applet(kAppletCalculate);
    get_applet(s.hi, LEFT)->OnDataReceive(pack_calculate(5, 7));
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 5);
    REQUIRE(((packed >> 8) & 0xFF) == 7);
}

TEST_CASE("calculate C2: MIN selects lesser of In(0), In(1)", "[calculate]") {
    auto s = setup_applet(kAppletCalculate);
    calculate_set_op(s.hi, 0, 0);  // MIN both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C3: MAX selects greater of In(0), In(1)", "[calculate]") {
    auto s = setup_applet(kAppletCalculate);
    calculate_set_op(s.hi, 1, 1);  // MAX both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(4.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(4.0f).margin(0.01f));
}

TEST_CASE("calculate C4: SUM clamps at HEMISPHERE_MAX_CV", "[calculate]") {
    auto s = setup_applet(kAppletCalculate);
    calculate_set_op(s.hi, 2, 2);  // SUM both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 6.0f, 8);   // = HEMISPHERE_MAX_CV (6V)
    set_cv(s.bus, LEFT, 1, 0.5f, 8);   // would push past max
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(6.0f).margin(0.01f));
}

TEST_CASE("calculate C5: SUM clamps at HEMISPHERE_MIN_CV", "[calculate]") {
    auto s = setup_applet(kAppletCalculate);
    calculate_set_op(s.hi, 2, 2);  // SUM both channels

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, -6.0f, 8);  // = HEMISPHERE_MIN_CV (-6V)
    set_cv(s.bus, LEFT, 1, -0.5f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(-6.0f).margin(0.01f));
}

TEST_CASE("calculate C6: DIFF returns absolute difference", "[calculate]") {
    auto s = setup_applet(kAppletCalculate);
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
    auto s = setup_applet(kAppletCalculate);
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
    auto s = setup_applet(kAppletCalculate);
    calculate_set_op(s.hi, 0, 1);  // op[0]=MIN, op[1]=MAX

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    set_cv(s.bus, LEFT, 1, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(1.0f).margin(0.01f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.01f));
}

TEST_CASE("calculate C9: S&H output stays at zero before clock", "[calculate]") {
    auto s = setup_applet(kAppletCalculate);
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
    auto s = setup_applet(kAppletCalculate);
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
    auto s = setup_applet(kAppletCalculate);
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
    auto s = setup_applet(kAppletCalculate);
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
    auto s = setup_applet(kAppletBrancher);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0x7F) == 50);
}

TEST_CASE("brancher B9: serialise round-trip preserves p", "[brancher]") {
    auto s = setup_applet(kAppletBrancher);
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(73));
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0x7F) == 73);
}

TEST_CASE("brancher B2: p=100 always routes gate to output 0", "[brancher]") {
    auto s = setup_applet(kAppletBrancher);
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
    auto s = setup_applet(kAppletBrancher);
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
    auto s = setup_applet(kAppletBrancher);
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

TEST_CASE("brancher B6: Clock(1) toggles flip-flop choice", "[brancher]") {
    // Vendor: Clock(1) sets flipflopmode=true and re-rolls choice. Output stays
    // high (GateOut(choice, flipflopmode)) without further Clock(0). The next
    // Clock(1) re-rolls choice again. With p=100 the roll always selects 0;
    // with p=0 always 1. We exercise both boundaries to observe a toggle.
    auto s = setup_applet(kAppletBrancher);

    // First Clock(1) at p=100. choice rolls to 0. flipflopmode=true. Output 0
    // stays high after the clock fires.
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);
    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 1, 0, 8);  // Clock(1) edge: single-sample on channel B
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // Second Clock(1) at p=0. choice rolls to 1. flipflopmode still true (re-set
    // each Clock(1)). Output 1 goes high; output 0 goes low.
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(0));
    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 1, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("brancher B7: OnButtonPress flips choice before next gate", "[brancher]") {
    // Vendor: OnButtonPress sets choice = 1 - choice. So if we press the
    // button BEFORE a sustained-high gate's next step, GateOut writes to the
    // FLIPPED output.
    //
    // Sequence:
    //   Step 1: Raise gate. Clock(0) fires once (rising edge), choice rolls
    //           to 0 at p=100, output 0 high.
    //   Button press: choice flips from 0 to 1.
    //   Step 2: Keep gate high. NO new rising edge (prev_high stays true
    //           across the buffer), so Clock(0) does not fire. The bottom
    //           branch !clocked || flipflopmode still drives GateOut(choice,
    //           Gate(0)=true), which now writes to output 1 (flipped).
    auto s = setup_applet(kAppletBrancher);
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);

    // Step 1: gate rising edge + sustained high. choice rolls to 0.
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // Flip choice via button press.
    get_applet(s.hi, LEFT)->OnButtonPress();

    // Step 2: gate stays high; no new rising edge. GateOut writes to choice
    // (now 1).
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("brancher B8: OnEncoderMove clamps p to [0, 100]", "[brancher]") {
    // Vendor: OnEncoderMove(direction) { p = constrain(p + direction, 0, 100); }
    // Start at p=50 (set by Start()). Push 30 calls of +5 to overshoot 100.
    // Push 30 calls of -5 to undershoot 0. Both bounds clamp.
    auto s = setup_applet(kAppletBrancher);
    auto* applet = get_applet(s.hi, LEFT);

    for (int i = 0; i < 30; ++i) applet->OnEncoderMove(+5);
    REQUIRE((applet->OnDataRequest() & 0x7F) == 100);

    for (int i = 0; i < 30; ++i) applet->OnEncoderMove(-5);
    REQUIRE((applet->OnDataRequest() & 0x7F) == 0);
}

TEST_CASE("brancher B4: p=50 yields ~50/50 routing over 1000 clocks", "[brancher]") {
    // Statistical assertion. Seeded xorshift32 gives reproducible output; 1000
    // rolls at p=50 should fall in [460, 540] for either output, well within
    // statistical tolerance (~3 standard deviations).
    auto s = setup_applet(kAppletBrancher);
    get_applet(s.hi, LEFT)->OnDataReceive(pack_brancher(50));
    seed_hem_rng(0xDEADBEEF);

    int count_0 = 0, count_1 = 0;
    for (int trial = 0; trial < 1000; ++trial) {
        // Sustained high gate produces exactly one Clock(0) rising edge per
        // buffer transition (rising-edge detection requires prev_high=false at
        // the start of the buffer).
        clear_bus(s.bus);
        hold_gate(s.bus, LEFT, 0, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        bool out0 = read_gate_at(s.bus, LEFT, 0, 0, 8);
        bool out1 = read_gate_at(s.bus, LEFT, 1, 0, 8);
        if (out0 && !out1) ++count_0;
        if (out1 && !out0) ++count_1;

        // Drop the gate so the next iteration sees a fresh rising edge.
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }

    REQUIRE(count_0 >= 460);
    REQUIRE(count_0 <= 540);
    REQUIRE(count_1 >= 460);
    REQUIRE(count_1 <= 540);
    REQUIRE(count_0 + count_1 >= 950);  // some rolls may land on neither output
}

TEST_CASE("logic L1: Start defaults are AND, XOR", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 0);  // AND
    REQUIRE(((packed >> 8) & 0xFF) == 2);  // XOR
}

TEST_CASE("logic L2: AND outputs s1 AND s2", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 0, 0);  // AND both

    // s1=low, s2=low -> 0
    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // s1=high, s2=low -> 0
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // s1=low, s2=high -> 0
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // s1=high, s2=high -> 1
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L3: OR outputs s1 OR s2", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 1, 1);

    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L4: XOR outputs s1 XOR s2", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 2, 2);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);  // s1=high only
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);  // both high -> XOR=0
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("logic L5: NAND, NOR, XNOR invert AND/OR/XOR", "[logic]") {
    auto s = setup_applet(kAppletLogic);

    // NAND s1=s2=high -> 0 (inverted AND)
    logic_set_op(s.hi, 3, 3);
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // NOR s1=s2=low -> 1 (inverted OR)
    logic_set_op(s.hi, 4, 4);
    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // XNOR s1=s2=high -> 1 (inverted XOR)
    logic_set_op(s.hi, 5, 5);
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L6: CV-controlled mode picks op from CV", "[logic]") {
    // Vendor Logic.h:62-67: when operation[ch] == 6, the actual op is selected
    // from CV by scaling abs(In(ch)) to [0, 5] and using that as the op index.
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 6, 6);  // CV-controlled both channels

    // CV near 0 -> idx 0 -> AND. s1=s2=high -> 1.
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    // No CV written; In(0) == 0 -> idx 0 -> AND.
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L7: serialise round-trip preserves ops", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    get_applet(s.hi, LEFT)->OnDataReceive(pack_logic(5, 3));
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 5);
    REQUIRE(((packed >> 8) & 0xFF) == 3);
}

// AttenuateOffset notes (vendor AttenuateOffset.h):
//   ATTENOFF_MAX_LEVEL = 63 (NOT 100; the original plan was authored against an
//   assumed value of 100). All test cases below use the real vendor constants:
//   unity gain = level 63, level bias in packed data = 126, level constraint
//   range = [-126, +126]. Signal formula:
//     out = clamp(Proportion(level, 63, In) + offset * 128, +/- HEMISPHERE_MAX_CV)
//   where ATTENOFF_INCREMENTS = 128 hem units per semitone (ONE_OCTAVE / 12).
TEST_CASE("atten_off A1: Start defaults level[0]=level[1]=63, offset=0", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int off0 = (int)((packed)       & 0x1FF) - 256;
    int off1 = (int)((packed >> 10) & 0x1FF) - 256;
    int lev0 = (int)((packed >> 19) & 0xFF)  - 126;  // ATTENOFF_MAX_LEVEL*2 bias
    int lev1 = (int)((packed >> 27) & 0xFF)  - 126;
    bool mix = ((packed >> 35) & 0x1) != 0;
    REQUIRE(off0 == 0);
    REQUIRE(off1 == 0);
    REQUIRE(lev0 == 63);
    REQUIRE(lev1 == 63);
    REQUIRE(mix == false);
}

TEST_CASE("atten_off A2: unity gain passes signal through", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 0, 0, 63, 63, false);  // level=63 == 100%

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.05f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.05f));
}

TEST_CASE("atten_off A3: ~50% level halves signal", "[atten_off]") {
    // Proportion(31, 63, In) = 31 * In / 63 ~= 0.492 * In. Use 4V input so the
    // rounding error stays under the 0.1V margin.
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 0, 0, 31, 31, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 4.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.1f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(2.0f).margin(0.1f));
}

TEST_CASE("atten_off A4: -100% level inverts signal", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 0, 0, -63, -63, false);  // -100% gain

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(-2.0f).margin(0.05f));
}

TEST_CASE("atten_off A5: positive offset shifts signal up", "[atten_off]") {
    // Offset is in semitones (ATTENOFF_INCREMENTS = ONE_OCTAVE / 12 = 128 hem units).
    // offset=12 -> +1V shift.
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 12, 0, 63, 63, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.05f));
}

TEST_CASE("atten_off A6: output clamps at HEMISPHERE_MAX_CV", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    // 100% gain, offset +12 semitones (=1V); input 6V; signal 7V before clamp; expected 6V.
    atten_off_set(s.hi, 12, 0, 63, 63, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 6.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(6.0f).margin(0.05f));
}

TEST_CASE("atten_off A7: mix mode sums Out(0) into Out(1)", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    // Out(0) computes signal_0 = 100% * 1V + 0 = 1V. Out(1) computes signal_1 =
    // 100% * 2V + 0 = 2V. With mix=true, Out(1) becomes signal_0 + signal_1 = 3V.
    atten_off_set(s.hi, 0, 0, 63, 63, true);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    set_cv(s.bus, LEFT, 1, 2.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(1.0f).margin(0.05f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.05f));
}

TEST_CASE("atten_off A8: serialise round-trip preserves all fields", "[atten_off]") {
    // Level values must stay inside [-126, +126] or OnDataReceive will clamp.
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, -12, 24, -50, 100, true);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int off0 = (int)((packed)       & 0x1FF) - 256;
    int off1 = (int)((packed >> 10) & 0x1FF) - 256;
    int lev0 = (int)((packed >> 19) & 0xFF)  - 126;
    int lev1 = (int)((packed >> 27) & 0xFF)  - 126;
    bool mix = ((packed >> 35) & 0x1) != 0;

    REQUIRE(off0 == -12);
    REQUIRE(off1 == 24);
    REQUIRE(lev0 == -50);
    REQUIRE(lev1 == 100);
    REQUIRE(mix == true);
}

TEST_CASE("slew SL1: Start defaults rise=50, fall=50", "[slew]") {
    auto s = setup_applet(kAppletSlew);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 50);
    REQUIRE(((packed >> 8) & 0xFF) == 50);
}

TEST_CASE("slew SL2: zero rise/fall is instant follower", "[slew]") {
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 0, 0);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(3.0f).margin(0.5f));
}

TEST_CASE("slew SL3: gate defeats slew", "[slew]") {
    // Vendor Slew.h: if (Gate(ch)) signal[ch] = input. Instant jump.
    // HEM_SLEW_MAX_VALUE is 200 in vendor, so max-slew uses 200.
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 200, 200);  // max slew (slow)

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    hold_gate(s.bus, LEFT, 0, 8);   // defeat
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(3.0f).margin(0.5f));
}

TEST_CASE("slew SL4: high rise slows attack to target", "[slew]") {
    // HEM_SLEW_MAX_VALUE is 200 in vendor, so max-slew uses 200.
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 200, 200);

    // Single step at the target: output should be far below the input
    // because slew is at maximum.
    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 5.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) < 4.5f);

    // After enough steps, output approaches the target.
    // Vendor formula at HEM_SLEW_MAX_VALUE=200 with segment=200 yields
    // ~2360 simfloat per tick (constant) toward a 5V target of 125.8M
    // simfloat. The shim runs ~10 Controller ticks per step()
    // (ticks_this_step = numFrames / 3 with numFrames = 32). To converge
    // within 0.5V of 5V (i.e. >= 4.5V = ~113M simfloat) we need
    // 113M / (10 * 2360) ~= 4800 step()s. 5000 gives a small safety
    // margin. The plan's hint to bump 200 to 500 was insufficient since
    // vendor HEM_SLEW_MAX_VALUE is 200 (not the plan-assumed 100), making
    // true max-slew an order of magnitude slower than the plan modeled.
    for (int i = 0; i < 5000; ++i) {
        clear_bus(s.bus);
        set_cv(s.bus, LEFT, 0, 5.0f, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(5.0f).margin(0.5f));
}

TEST_CASE("slew SL5: serialise round-trip preserves rise/fall", "[slew]") {
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 17, 83);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 17);
    REQUIRE(((packed >> 8) & 0xFF) == 83);
}

TEST_CASE("burst B1: Start defaults number=4, spacing=50, div=1, jitter=0, accel=0", "[burst]") {
    auto s = setup_applet(kAppletBurst);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int number  = (int)((packed) & 0xFF);
    int spacing = (int)((packed >> 8) & 0xFF);
    int div     = (int)((packed >> 16) & 0xFF) - 8;
    int jitter  = (int)((packed >> 24) & 0xFF);
    int accel   = (int)((packed >> 32) & 0xFF);
    REQUIRE(number  == 4);
    REQUIRE(spacing == 50);
    REQUIRE(div     == 1);
    REQUIRE(jitter  == 0);
    REQUIRE(accel   == 0);
}

TEST_CASE("burst B2: Clock(1) fires a burst that produces gate pulses on output 0", "[burst]") {
    // Vendor reality: Burst::Controller() triggers a new burst set on Clock(1)
    // (the second gate input), not Clock(0). Clock(0) is only used to learn
    // tempo for the "clocked" spacing mode. After a Clock(1) edge, Burst emits
    // `number` pulses on output 0 at `spacing` intervals. Run for many steps
    // after the clock; assert that output 0 went high at least once.
    auto s = setup_applet(kAppletBurst);
    burst_set(s.hi, 2, 50, 1, 0, 0);  // 2 pulses, default spacing
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 1, 0, 8);  // Clock(1) edge fires the burst
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    bool saw_pulse = read_gate_at(s.bus, LEFT, 0, 0, 8);
    // The burst may fire on a later step; advance many buffers and watch
    // for the gate output going high.
    for (int i = 0; i < 100 && !saw_pulse; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_pulse = true;
    }
    REQUIRE(saw_pulse);
}

TEST_CASE("burst B3: serialise round-trip preserves all fields", "[burst]") {
    auto s = setup_applet(kAppletBurst);
    burst_set(s.hi, 7, 100, -3, 25, 10);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int number  = (int)((packed) & 0xFF);
    int spacing = (int)((packed >> 8) & 0xFF);
    int div     = (int)((packed >> 16) & 0xFF) - 8;
    int jitter  = (int)((packed >> 24) & 0xFF);
    int accel   = (int)((packed >> 32) & 0xFF);
    REQUIRE(number  == 7);
    REQUIRE(spacing == 100);
    REQUIRE(div     == -3);
    REQUIRE(jitter  == 25);
    REQUIRE(accel   == 10);
}

TEST_CASE("compare CM1: Start defaults level=128", "[compare]") {
    auto s = setup_applet(kAppletCompare);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF) == 128);
}

TEST_CASE("compare CM2: In(0) above threshold drives gate 0 high, gate 1 low", "[compare]") {
    // Vendor: threshold = Proportion(level, HEM_COMPARE_MAX_VALUE, HEMISPHERE_MAX_CV).
    // With level=128, MAX=255, HEMISPHERE_MAX_CV=9216, threshold ~= 4625 hem
    // units (~3.01V). 4V on CV1 with CV2=0 puts In(0) above mod_cv.
    auto s = setup_applet(kAppletCompare);
    compare_set(s.hi, 128);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);
}

TEST_CASE("compare CM3: In(0) below threshold drives gate 0 low, gate 1 high", "[compare]") {
    // 2V on CV1 with CV2=0 puts In(0) (~3072) below threshold (~4625).
    auto s = setup_applet(kAppletCompare);
    compare_set(s.hi, 128);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);
}

TEST_CASE("compare CM4: serialise round-trip preserves level", "[compare]") {
    auto s = setup_applet(kAppletCompare);
    compare_set(s.hi, 200);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF) == 200);
}
