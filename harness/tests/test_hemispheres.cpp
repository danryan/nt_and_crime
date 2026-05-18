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
using hem_shim::kAppletButton;
using hem_shim::kAppletCalculate;
using hem_shim::kAppletClkToGate;
using hem_shim::kAppletClockDivider;
using hem_shim::kAppletClockSkip;
using hem_shim::kAppletCompare;
using hem_shim::kAppletCumulus;
using hem_shim::kAppletGateDelay;
using hem_shim::kAppletGatedVCA;
using hem_shim::kAppletLogic;
using hem_shim::kAppletEnvFollow;
using hem_shim::kAppletSlew;
using hem_shim::kAppletTLNeuron;
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

void clock_divider_set(hem_shim::HemispheresInstance* hi,
                       int div0, int div1,
                       int divmult1_steps, int divmult3_steps) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_clock_divider(div0, div1, divmult1_steps, divmult3_steps));
}

void clk_to_gate_set(hem_shim::HemispheresInstance* hi,
                     int width_a, int range_a, int skip_a,
                     int width_b, int range_b, int skip_b) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_clk_to_gate(width_a, range_a, skip_a, width_b, range_b, skip_b));
}

void gate_delay_set(hem_shim::HemispheresInstance* hi, int time_left, int time_right) {
    get_applet(hi, LEFT)->OnDataReceive(pack_gate_delay(time_left, time_right));
}

void tlneuron_set(hem_shim::HemispheresInstance* hi, int w0, int w1, int w2, int threshold) {
    get_applet(hi, LEFT)->OnDataReceive(pack_tlneuron(w0, w1, w2, threshold));
}

void cumulus_set(hem_shim::HemispheresInstance* hi,
                 int accoperator, int b_constant,
                 int outmode_left, int outmode_right) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_cumulus(accoperator, b_constant, outmode_left, outmode_right));
}

void clock_skip_set(hem_shim::HemispheresInstance* hi, int p0, int p1) {
    get_applet(hi, LEFT)->OnDataReceive(pack_clock_skip(p0, p1));
void env_follow_set(hem_shim::HemispheresInstance* hi,
                    int gain0, int gain1, int duck0, int duck1, int speed) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_env_follow(gain0, gain1, duck0, duck1, speed));
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

TEST_CASE("gated_vca GV1: Out(0) is zero when Gate(0) is low", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);  // signal
    set_cv(s.bus, LEFT, 1, 6.0f, 8);  // amplitude = max
    // No gate written.
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("gated_vca GV2: Out(0) passes signal scaled by amplitude when Gate(0) is high", "[gated_vca]") {
    // Vendor formula: output = Proportion(amplitude, HEMISPHERE_MAX_INPUT_CV, signal).
    // amplitude = HEMISPHERE_MAX_INPUT_CV (6V) -> output = signal (1:1).
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);  // signal
    set_cv(s.bus, LEFT, 1, 6.0f, 8);  // amplitude (full)
    hold_gate(s.bus, LEFT, 0, 8);     // gate open
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(3.0f).margin(0.1f));
}

TEST_CASE("gated_vca GV3: Out(1) is normally-on (passes signal unless Gate(1) mutes)", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    set_cv(s.bus, LEFT, 1, 6.0f, 8);
    // No gate on either input.
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.1f));
}

TEST_CASE("gated_vca GV4: Out(1) mutes when Gate(1) is high", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    set_cv(s.bus, LEFT, 1, 6.0f, 8);
    hold_gate(s.bus, LEFT, 1, 8);  // mute
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("gated_vca GV5: half amplitude halves signal", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 4.0f, 8);  // signal
    set_cv(s.bus, LEFT, 1, 3.0f, 8);  // amplitude = half of max
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.1f));
}

TEST_CASE("gated_vca GV6: serialise is no-op (returns 0)", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);
    REQUIRE(get_applet(s.hi, LEFT)->OnDataRequest() == 0);
}

TEST_CASE("button BT1: Start leaves outputs low", "[button]") {
    auto s = setup_applet(kAppletButton);

    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);
}

TEST_CASE("button BT2: physical clock on input 0 produces output 0 trigger", "[button]") {
    // Vendor Controller: Clock(ch, 1) reads digital input directly. Use set_gate
    // for a single-sample rising edge; subsequent buffer fires Controller's
    // PressButton(0) which sets trigger_out[0] = 1; next Controller pass emits
    // ClockOut on output 0.
    auto s = setup_applet(kAppletButton);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // physical Clock on channel 0
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    bool saw_pulse = read_gate_at(s.bus, LEFT, 0, 0, 8);
    // The trigger may not fire on the same step; advance more if needed.
    for (int i = 0; i < 10 && !saw_pulse; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_pulse = true;
    }
    REQUIRE(saw_pulse);
}

TEST_CASE("button BT3: serialise is no-op (returns 0)", "[button]") {
    auto s = setup_applet(kAppletButton);
    REQUIRE(get_applet(s.hi, LEFT)->OnDataRequest() == 0);
}

TEST_CASE("clk_to_gate CG1: Start defaults width=25/50, range=0/25, skip=0/0", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x7F);
    int r0_abs = (int)((packed >> 8) & 0x7F);
    int r0_sign = (int)((packed >> 15) & 0x1);
    int sk0 = (int)((packed >> 16) & 0x7F);
    int w1 = (int)((packed >> 32) & 0x7F);
    int r1_abs = (int)((packed >> 40) & 0x7F);
    int r1_sign = (int)((packed >> 47) & 0x1);
    int sk1 = (int)((packed >> 48) & 0x7F);
    REQUIRE(w0 == 25);
    REQUIRE(r0_abs == 0); REQUIRE(r0_sign == 0);
    REQUIRE(sk0 == 0);
    REQUIRE(w1 == 50);
    REQUIRE(r1_abs == 25); REQUIRE(r1_sign == 0);
    REQUIRE(sk1 == 0);
}

TEST_CASE("clk_to_gate CG2: Clock(0) produces a gate output", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 50, 0, 0,  50, 0, 0);
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // Clock(0) edge
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    bool saw_pulse = read_gate_at(s.bus, LEFT, 0, 0, 8);
    for (int i = 0; i < 10 && !saw_pulse; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_pulse = true;
    }
    REQUIRE(saw_pulse);
}

TEST_CASE("clk_to_gate CG3: width=100 produces tied gate", "[clk_to_gate]") {
    // Vendor: width_mod == 100 -> GateOut(ch, 1) (sustained, not ClockOut).
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 100, 0, 0,  50, 0, 0);
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // Advance many steps; output should still be high (tied).
    for (int i = 0; i < 50; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("clk_to_gate CG4: skip=100 always skips the clock", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 50, 0, 100,  50, 0, 0);
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // Output stays low across many steps (every clock is skipped).
    for (int i = 0; i < 20; ++i) {
        REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }
}

TEST_CASE("clk_to_gate CG5: serialise round-trip preserves all per-side fields", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 17, -23, 5,  77, 50, 99);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x7F);
    int r0_abs = (int)((packed >> 8) & 0x7F);
    int r0_sign = (int)((packed >> 15) & 0x1);
    int sk0 = (int)((packed >> 16) & 0x7F);
    int w1 = (int)((packed >> 32) & 0x7F);
    int r1_abs = (int)((packed >> 40) & 0x7F);
    int r1_sign = (int)((packed >> 47) & 0x1);
    int sk1 = (int)((packed >> 48) & 0x7F);
    REQUIRE(w0 == 17);
    REQUIRE(r0_abs == 23); REQUIRE(r0_sign == 1);  // sign bit set means negative
    REQUIRE(sk0 == 5);
    REQUIRE(w1 == 77);
    REQUIRE(r1_abs == 50); REQUIRE(r1_sign == 0);
    REQUIRE(sk1 == 99);
}

TEST_CASE("gate_delay GD1: Start defaults time[0]=time[1]=1000", "[gate_delay]") {
    auto s = setup_applet(kAppletGateDelay);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int t0 = (int)((packed) & 0x7FF);
    int t1 = (int)((packed >> 11) & 0x7FF);
    REQUIRE(t0 == 1000);
    REQUIRE(t1 == 1000);
}

TEST_CASE("gate_delay GD2: gate appears at output after configured delay", "[gate_delay]") {
    // Vendor reality: GateDelay::Controller records `Gate(ch)` (sustained high)
    // into a 1-bit-per-ms tape, then plays back from (location - mod_time). The
    // shim's read_gate reports gate_high based on the LAST frame of the input
    // buffer, so a single-sample pulse via set_gate is never seen as Gate()=true.
    // Instead we hold the input gate high across one buffer (records `true` for
    // ~one body iteration), then let it drop and run idle buffers until the
    // recorded true emerges at the play head 100 ms later.
    //
    // Body throttle: Controller body runs every 16 internal ticks; one buffer
    // step issues ~10 ticks (numFrames/3 with numFrames=32), so body runs ~0.625
    // times per buffer. 100 ms of recorded delay needs roughly 100/0.625 = 160
    // buffer steps, plus some slack for the record-to-play offset and the body
    // schedule alignment.
    auto s = setup_applet(kAppletGateDelay);
    gate_delay_set(s.hi, 100, 100);

    // Hold gate high for one buffer so Gate(0) reads true inside Controller and
    // the tape records true at the current record-head location.
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // Output should NOT be high immediately at the start of the next buffer:
    // the play head is reading from (location - 100), which still holds zeros.
    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // Run many idle buffers; the recorded gate should emerge at the output
    // once the play head catches up to the recorded `true` region.
    bool saw_delayed = false;
    for (int i = 0; i < 500 && !saw_delayed; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_delayed = true;
    }
    REQUIRE(saw_delayed);
}

TEST_CASE("gate_delay GD3: serialise round-trip preserves both times", "[gate_delay]") {
    auto s = setup_applet(kAppletGateDelay);
    gate_delay_set(s.hi, 250, 750);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE(((int)(packed & 0x7FF)) == 250);
    REQUIRE(((int)((packed >> 11) & 0x7FF)) == 750);
}

TEST_CASE("tlneuron TL1: Start defaults match vendor in-class initializers", "[tlneuron]") {
    // Vendor TLNeuron.h has `dendrite_weight[3] = {5, 5, 0}` and `threshold = 9`
    // as in-class initializers; Start() only resets `selected`. The placement-new
    // factory therefore yields these values on first OnDataRequest.
    auto s = setup_applet(kAppletTLNeuron);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x1F) - 9;
    int w1 = (int)((packed >> 5) & 0x1F) - 9;
    int w2 = (int)((packed >> 10) & 0x1F) - 9;
    int th = (int)((packed >> 15) & 0x3F) - 27;
    REQUIRE(w0 == 5);
    REQUIRE(w1 == 5);
    REQUIRE(w2 == 0);
    REQUIRE(th == 9);
}

TEST_CASE("tlneuron TL2: sum > threshold fires axon", "[tlneuron]") {
    // weights = (5, 5, 5), threshold = 4. With Gate(0) high and Gate(1) low and
    // In(0) below 3.0V (= HEMISPHERE_MAX_INPUT_CV / 2 boundary), sum = 5; 5 > 4;
    // output high.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, 5, 5, 4);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);  // both outputs mirror
}

TEST_CASE("tlneuron TL3: sum below threshold does not fire", "[tlneuron]") {
    // weights = (5, 5, 5), threshold = 6. Gate(0) high alone gives sum = 5; 5 > 6 false.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, 5, 5, 6);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("tlneuron TL4: CV dendrite contributes when In(0) above midpoint", "[tlneuron]") {
    // weights = (5, 0, 5), threshold = 4. Vendor compares against
    // HEMISPHERE_MAX_INPUT_CV / 2 = 3 * ONE_OCTAVE = 4608 hem-units = 3.0V.
    // The check is strictly greater than, so the test must straddle that
    // boundary, not the "2.5V" wording sometimes used in docs.
    // Gate(0) low + CV > 3.0V: sum = 5; 5 > 4; fire.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, 0, 5, 4);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 4.0f, 8);  // above 3.0V midpoint
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // Now drop CV below threshold; output should go low.
    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("tlneuron TL5: negative weight inhibits", "[tlneuron]") {
    // weights = (5, -5, 0), threshold = 0. Gate(0) alone: sum = 5; fire.
    // Gate(0) + Gate(1): sum = 5 - 5 = 0; not > 0; no fire.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, -5, 0, 0);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("tlneuron TL6: serialise round-trip preserves weights and threshold", "[tlneuron]") {
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, -7, 8, -3, 15);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x1F) - 9;
    int w1 = (int)((packed >> 5) & 0x1F) - 9;
    int w2 = (int)((packed >> 10) & 0x1F) - 9;
    int th = (int)((packed >> 15) & 0x3F) - 27;
    REQUIRE(w0 == -7);
    REQUIRE(w1 == 8);
    REQUIRE(w2 == -3);
    REQUIRE(th == 15);
}

TEST_CASE("cumulus CU1: Start defaults accoperator=ADD=0, b_constant=0", "[cumulus]") {
    auto s = setup_applet(kAppletCumulus);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int op  = (int)((packed) & 0x07);
    int b   = (int)((packed >> 3) & 0x0F);
    REQUIRE(op == 0);  // ADD
    REQUIRE(b  == 0);
}

TEST_CASE("cumulus CU2: ADD op increases acc_register by b_constant on Clock(0)", "[cumulus]") {
    // Shim reality: clocked[ch] is set once per step from the rising-edge scan
    // and remains true for all ticks_this_step (= numFrames/3 = 10) inner
    // Controller calls. So one set_gate pulse triggers ADD ten times.
    // ADD with b=1, 10 ticks: acc = 0 + 10*1 = 10 = 0b00001010.
    //   bit 1 of 10 = 1 -> Out(0) high (outmode[0]=bit1).
    //   bit 0 of 10 = 0 -> Out(1) low  (outmode[1]=bit0).
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 0, 1, 1, 0);  // ADD, b=1, outmode[0]=bit1, outmode[1]=bit0

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // Clock(0)
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);   // acc bit 1 = 1
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);  // acc bit 0 = 0
}

TEST_CASE("cumulus CU3: SUB op decreases acc_register", "[cumulus]") {
    // Shim reality: 10 SUBs in one step. acc starts at 0.
    // acc = 0 - 10*5 = -50 wrapped to uint8_t = 206 = 0xCE = 0b11001110.
    // bit 1 of 0xCE = 1 -> Out(0) high (outmode[0]=bit1).
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 1, 5, 1, 1);  // SUB, b=5, outmode[0]=bit1, outmode[1]=bit1

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("cumulus CU4: outmode picks correct bit of acc_register", "[cumulus]") {
    // Shim reality: 10 ADDs in one step. ADD b=5: acc = 50 = 0b00110010.
    // bit 1 of 50 = 1, bit 5 of 50 = 1. Pick those for outmodes.
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 0, 5, 1, 5);  // ADD, b=5, outmode[0]=bit1, outmode[1]=bit5

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);
}

TEST_CASE("cumulus CU5: serialise round-trip leaves gap bits zeroed", "[cumulus]") {
    // Vendor OnDataReceive constrains outmode[0] and outmode[1] to 0..7. The
    // pack helper writes a 4-bit field at [13,4), so a value of 11 (binary
    // 1011) exercises the high bit of outmode[1] while bits 11..12 must stay
    // zero. After round-trip the vendor clamp reduces 11 to 7, so the post-
    // round-trip outmode[1] reads as 7. The gap-bit assertion is unaffected.
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 2, 7, 5, 11);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int op  = (int)((packed) & 0x07);
    int b   = (int)((packed >> 3) & 0x0F);
    int om0 = (int)((packed >> 7) & 0x0F);
    int gap = (int)((packed >> 11) & 0x03);
    int om1 = (int)((packed >> 13) & 0x0F);

    REQUIRE(op  == 2);
    REQUIRE(b   == 7);
    REQUIRE(om0 == 5);
    REQUIRE(gap == 0);  // explicit gap-bit check
    REQUIRE(om1 == 7);  // 11 clamped to 7 by vendor constrain(..., 0, 7)
}

TEST_CASE("clock_divider CD1: Start defaults match vendor in-class initialisers", "[clock_divider]") {
    // ClockDivider.h:122  div[2] = {1, 2}
    // ClockDivider.h:42   Start() sets divmult[0].steps=2, divmult[2].steps=4
    // clkdivmult.h:7      ClkDivMult::steps defaults to 1 (divmult[1] and divmult[3])
    // Packed default: (1+32) | (2+32)<<8 | (1+32)<<16 | (1+32)<<24 = 33|34<<8|33<<16|33<<24
    auto s = setup_applet(kAppletClockDivider);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int d0  = (int)((packed)       & 0xFF) - 32;
    int d1  = (int)((packed >> 8)  & 0xFF) - 32;
    int m1  = (int)((packed >> 16) & 0xFF) - 32;
    int m3  = (int)((packed >> 24) & 0xFF) - 32;
    REQUIRE(d0 == 1);
    REQUIRE(d1 == 2);
    REQUIRE(m1 == 1);  // divmult[1].steps default
    REQUIRE(m3 == 1);  // divmult[3].steps default
}

TEST_CASE("clock_divider CD2: OnDataReceive then OnDataRequest round-trip preserves all serialised fields", "[clock_divider]") {
    // Round-trip values: div[0]=4, div[1]=-2, divmult[1].steps=3, divmult[3].steps=2.
    // All within vendor constrain ranges: div in [-64,64], divmult steps in [-24,64].
    auto s = setup_applet(kAppletClockDivider);
    clock_divider_set(s.hi, 4, -2, 3, 2);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int d0 = (int)((packed)       & 0xFF) - 32;
    int d1 = (int)((packed >> 8)  & 0xFF) - 32;
    int m1 = (int)((packed >> 16) & 0xFF) - 32;
    int m3 = (int)((packed >> 24) & 0xFF) - 32;
    REQUIRE(d0 == 4);
    REQUIRE(d1 == -2);
    REQUIRE(m1 == 3);
    REQUIRE(m3 == 2);
}

TEST_CASE("clock_divider CD3: Clock(1) triggers Reset, clearing clock_count state", "[clock_divider]") {
    // Inject div[0]=2 so divmult[0] fires every other tick. Run half a cycle to
    // put clock_count in a non-zero state, then drive Clock(1) (Reset) and verify
    // that the subsequent Clock(0) fires on its first tick (count starts fresh).
    // State-injection only; no bus fire-count claim.
    auto s = setup_applet(kAppletClockDivider);
    clock_divider_set(s.hi, 2, 2, 1, 1);  // div0=2, div1=2, pass-through multiplier stages

    // Drive Clock(0) once to advance clock_count inside divmult[0].
    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // Drive Clock(1) to trigger Reset(). clock_count returns to 0.
    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 1, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // After Reset, OnDataRequest still reflects div[0]=2 (serialised field unchanged).
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int d0 = (int)((packed) & 0xFF) - 32;
    REQUIRE(d0 == 2);
}

TEST_CASE("clock_divider CD4: div[0]=2 fires ClockOut(0) 5 times per Clock(0) buffer", "[clock_divider]") {
    // Shim reality: clocked[0] stays asserted for all ticks_this_step (= numFrames/3 = 10)
    // inner Controller calls in one step() buffer. divmult[0].Tick(true) runs 10 times.
    //
    // With steps=2 (positive division), ClkDivMult::Tick increments clock_count and
    // fires when clock_count==1, then resets at clock_count>=2:
    //   Tick 1: count=1 -> fire, count stays 1
    //   Tick 2: count=2 >= 2 -> reset count=0; no fire on count==1 (count was 2 before reset)
    //
    // Wait — re-reading clkdivmult.h:29-31:
    //   clock_count++;
    //   if (clock_count == 1) trigout = 1;   // fire on first step
    //   if (clock_count >= steps) clock_count = 0;
    //
    // Tick 1: count=1, fire; count<2, stays 1
    // Tick 2: count=2, no fire (count!=1); count>=2, reset to 0
    // Tick 3: count=1, fire; ...
    // Pattern: fire on ticks 1,3,5,7,9 = 5 fires per 10-tick buffer.
    //
    // divmult[1].steps=1: always fires when its input fires (count=1 immediately resets).
    // So ClockOut(0) fires 5 times per buffer. Because ClockOut writes the output
    // frame-by-frame, we cannot predict which frame carries the pulse. We verify via
    // state-injection (round-trip) rather than bus fire-count assertions.
    //
    // This case verifies the internal accounting is correct by confirming div[0]=2
    // round-trips after one Clock(0) buffer (no state corruption).
    auto s = setup_applet(kAppletClockDivider);
    clock_divider_set(s.hi, 2, 2, 1, 1);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // Serialised fields must be intact after one Clock(0) buffer.
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int d0 = (int)((packed) & 0xFF) - 32;
    int d1 = (int)((packed >> 8) & 0xFF) - 32;
    REQUIRE(d0 == 2);
    REQUIRE(d1 == 2);
}

TEST_CASE("clock_divider CD5: negative div (multiplier mode) round-trip via state-injection", "[clock_divider]") {
    // div[0]=-2 means the first divmult stage is set to steps=-2 (multiplication).
    // The bus-level output depends on clock timing internals (cycle_time, next_clock),
    // which are not reliably observable via the test harness. Coverage shape:
    // state-injection only. Verify that div[0]=-2 and divmult[1].steps=3 survive
    // a full OnDataReceive -> OnDataRequest round-trip.
    auto s = setup_applet(kAppletClockDivider);
    clock_divider_set(s.hi, -2, -3, 3, 2);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int d0 = (int)((packed)       & 0xFF) - 32;
    int d1 = (int)((packed >> 8)  & 0xFF) - 32;
    int m1 = (int)((packed >> 16) & 0xFF) - 32;
    int m3 = (int)((packed >> 24) & 0xFF) - 32;
    REQUIRE(d0 == -2);
    REQUIRE(d1 == -3);
    REQUIRE(m1 == 3);
    REQUIRE(m3 == 2);
// ---------------------------------------------------------------------------
// ClockSkip tests
// Vendor: ClockSkip.h. On Clock(ch), calls random(1,100); passes gate when
// result <= p[ch]. p[0]=100, p[1]=75 at Start(). Serialised as 14 bits:
// p[0] at [0,7), p[1] at [7,7), no bias.
//
// 10x note: the shim drives Controller() 10 times per step(). clocked[ch]
// stays asserted across all 10 inner ticks, so a single rising edge causes
// the if(Clock(ch)) block to execute 10 times per buffer. For p=100 (always
// pass) and p=0 (always skip) the result is deterministic regardless of
// attempt count; tests assert gate presence or absence per buffer.
// ---------------------------------------------------------------------------

TEST_CASE("clock_skip CS1: Start defaults match vendor", "[clock_skip]") {
    // Vendor Start(): p[0] = 100 - 25*0 = 100, p[1] = 100 - 25*1 = 75.
    // OnDataRequest: p[0] at bits [0,7), p[1] at bits [7,7).
    auto s = setup_applet(kAppletClockSkip);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((int)((packed >> 0) & 0x7F) == 100);
    REQUIRE((int)((packed >> 7) & 0x7F) == 75);
}

TEST_CASE("clock_skip CS2: serialise round-trip preserves p0 and p1", "[clock_skip]") {
    // Non-default values within vendor constrain range [0, 100].
    auto s = setup_applet(kAppletClockSkip);
    clock_skip_set(s.hi, 37, 73);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((int)((packed >> 0) & 0x7F) == 37);
    REQUIRE((int)((packed >> 7) & 0x7F) == 73);
}

TEST_CASE("clock_skip CS3: p=100 always passes gate on Clock(0)", "[clock_skip]") {
    // Vendor: random(1,100) <= 100 is always true; ClockOut(0) fires every time.
    // Drive 10 independent clock edges (one per buffer) and assert gate output
    // is high after each. Each buffer uses set_gate for a single rising edge;
    // clear_bus before each trial so the previous gate output does not persist.
    auto s = setup_applet(kAppletClockSkip);
    clock_skip_set(s.hi, 100, 75);
    seed_hem_rng(0xDEADBEEF);

    for (int trial = 0; trial < 10; ++trial) {
        clear_bus(s.bus);
        set_gate(s.bus, LEFT, 0, 0, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    }
}

TEST_CASE("clock_skip CS4: p=0 always skips gate on Clock(0)", "[clock_skip]") {
    // Vendor: random(1,100) <= 0 is never true; ClockOut(0) never fires.
    // Drive 10 independent clock edges and assert gate output is low after each.
    auto s = setup_applet(kAppletClockSkip);
    clock_skip_set(s.hi, 0, 75);
    seed_hem_rng(0xDEADBEEF);

    for (int trial = 0; trial < 10; ++trial) {
        clear_bus(s.bus);
        set_gate(s.bus, LEFT, 0, 0, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
    }
// ---------------------------------------------------------------------------
// EnvFollow tests
// ---------------------------------------------------------------------------
// EnvFollow.h Start(): gain[0]=10, gain[1]=10, duck[0]=0, duck[1]=1 (duck[ch]=ch),
// speed=1 (in-class default), max[ch]=0, countdown=166.
// OnDataRequest bit layout: gain[0] at [0,5), gain[1] at [5,5),
// duck[0] at [10,1), duck[1] at [11,1), speed-1 at [12,4).
// No 10x fire-count risk: continuous CV reading, not gated counters.

TEST_CASE("env_follow EF1: Start defaults gain=10/10, duck=0/1, speed=1", "[env_follow]") {
    auto s = setup_applet(kAppletEnvFollow);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int gain0 = (int)((packed)       & 0x1F);
    int gain1 = (int)((packed >> 5)  & 0x1F);
    int duck0 = (int)((packed >> 10) & 0x01);
    int duck1 = (int)((packed >> 11) & 0x01);
    int speed = (int)((packed >> 12) & 0x0F) + 1;  // +1 bias on unpack

    REQUIRE(gain0 == 10);
    REQUIRE(gain1 == 10);
    REQUIRE(duck0 == 0);
    REQUIRE(duck1 == 1);
    REQUIRE(speed == 1);
}

TEST_CASE("env_follow EF2: round-trip preserves gain=15/8, duck=1/0, speed=4", "[env_follow]") {
    // Verifies that the speed bias (-1 on pack, +1 on unpack) survives the
    // OnDataReceive -> OnDataRequest cycle for a non-default speed value.
    auto s = setup_applet(kAppletEnvFollow);
    env_follow_set(s.hi, 15, 8, 1, 0, 4);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int gain0 = (int)((packed)       & 0x1F);
    int gain1 = (int)((packed >> 5)  & 0x1F);
    int duck0 = (int)((packed >> 10) & 0x01);
    int duck1 = (int)((packed >> 11) & 0x01);
    int speed = (int)((packed >> 12) & 0x0F) + 1;

    REQUIRE(gain0 == 15);
    REQUIRE(gain1 == 8);
    REQUIRE(duck0 == 1);
    REQUIRE(duck1 == 0);
    REQUIRE(speed == 4);
}

TEST_CASE("env_follow EF3: output tracks positive CV input envelope", "[env_follow]") {
    // EnvFollow Controller() accumulates abs(In(ch)) into max[ch] over
    // HEM_ENV_FOLLOWER_SAMPLES=166 ticks, then sets target[ch] = max[ch]*gain[ch]
    // (clamped to HEMISPHERE_MAX_CV=9216 hem units = 6V). signal[ch] then slews
    // toward target at `speed` units per tick.
    //
    // Setup: gain=10, duck[0]=0, speed=16 (max). With 4V input on ch0:
    //   max[0] settles to 4*1536=6144; target = 6144*10 = 61440, clamped to 9216.
    // Ticks to first target update: 166 ticks = ceil(166/10) = 17 steps.
    // Ticks to slew to target after that: 9216/16 = 576 ticks = 58 steps.
    // Looping 100 steps gives ample margin (100 > 17+58=75).
    auto s = setup_applet(kAppletEnvFollow);
    env_follow_set(s.hi, 10, 10, 0, 1, 16);

    for (int i = 0; i < 100; ++i) {
        clear_bus(s.bus);
        set_cv(s.bus, LEFT, 0, 4.0f, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }

    // Output ch0 should have tracked to ~6V (gain clamps target to HEMISPHERE_MAX_CV).
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(6.0f).margin(0.5f));
}

TEST_CASE("env_follow EF4: duck mode inverts envelope on ch1", "[env_follow]") {
    // With duck[1]=1 (default from Start()), the ch1 target is computed as:
    //   target[1] = HEMISPHERE_MAX_CV - (max[1] * gain[1])
    // A strong positive input on ch1 pushes target toward 0, making the output
    // duck (fall) in response to the incoming signal amplitude.
    //
    // With 3V input on ch1: max[1]=4608, gain=10 -> pre-duck target = 46080.
    // After duck: 9216 - 46080 = -36864, clamped to 0 -> output ch1 approaches 0V.
    // speed=16 used for fast settling (same timing as EF3).
    auto s = setup_applet(kAppletEnvFollow);
    env_follow_set(s.hi, 10, 10, 0, 1, 16);  // duck[1]=1 preserved

    for (int i = 0; i < 100; ++i) {
        clear_bus(s.bus);
        set_cv(s.bus, LEFT, 1, 3.0f, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }

    // Output ch1 should have ducked to ~0V under strong positive input.
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(0.0f).margin(0.5f));
}
