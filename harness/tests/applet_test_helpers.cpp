#include "applet_test_helpers.h"
#include "nt_runtime.h"
#include <cmath>
#include <cstring>

// hem_rng_state is defined in shim/src/globals.cpp and declared extern in
// shim/include/HemisphereApplet.h. We re-declare it here so the helper TU
// does not need to pull HemisphereApplet.h (which transitively drags in
// vendor applet headers via hemispheres_shim.h).
extern uint32_t hem_rng_state;

// Inner-tick override-global. Defined in shim/src/globals.cpp and
// declared extern in shim/include/hemispheres_shim.h. Re-declared here
// at file scope so the helper TU does not need to pull hemispheres_shim.h
// (which transitively drags in vendor applet headers).
namespace hem_shim { extern int inner_ticks_override; }

namespace hem_test {

int bus_index(HemSide side, int channel, HemAxis axis) {
    // Channels A,B (0,1) are left side; C,D (2,3) are right side.
    int slot = (side == LEFT) ? channel : (channel + 2);
    switch (axis) {
        case GATE_IN: return 1  + slot;  // 1..4
        case CV_IN:   return 5  + slot;  // 5..8
        case OUT:     return 13 + slot;  // 13..16
    }
    return 0;
}

void set_gate(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, GATE_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    slice[frame_offset] = 6.0f;  // above 0.5f gate threshold
}

void hold_gate(float* bus, HemSide side, int channel, int numFramesBy4) {
    int bidx = bus_index(side, channel, GATE_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = 6.0f;
}

void set_cv(float* bus, HemSide side, int channel, float volts, int numFramesBy4) {
    int bidx = bus_index(side, channel, CV_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = volts;
}

bool read_gate_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, OUT);
    int numFrames = numFramesBy4 * 4;
    return bus[(bidx - 1) * numFrames + frame_offset] > 0.5f;
}

float read_cv_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, OUT);
    int numFrames = numFramesBy4 * 4;
    return bus[(bidx - 1) * numFrames + frame_offset];
}

void select_applet(_NT_algorithm* alg, HemSide side, hem_shim::AppletIndex idx) {
    int param = (side == LEFT) ? 0 : 1;  // kHemSelLeft = 0, kHemSelRight = 1
    const_cast<int16_t*>(alg->v)[param] = (int16_t)idx;
}

int step_n_frames(nt::LoadedPlugin* loaded, _NT_algorithm* alg, float* bus, int n_samples) {
    const int framesPerStep = 32;
    int steps = (n_samples + framesPerStep - 1) / framesPerStep;
    for (int i = 0; i < steps; ++i) {
        loaded->factory->step(alg, bus, 8);
    }
    return steps * framesPerStep;
}

void step_n_inner_ticks(nt::LoadedPlugin* loaded, _NT_algorithm* alg,
                        float* bus, int N) {
    ::hem_shim::inner_ticks_override = N;  // file-scope extern declared above
    loaded->factory->step(alg, bus, 8);
    // inner_ticks_override consumed-and-cleared inside step()
}

int volts_to_int(float v) {
    return (int)std::lroundf(v * 1536.0f);
}

float int_to_volts(int hem_units) {
    return (float)hem_units / 1536.0f;
}

void seed_hem_rng(uint32_t seed) {
    hem_rng_state = seed;
}

uint64_t pack_calculate(int op_left, int op_right) {
    return ((uint64_t)(op_left  & 0xFF))
         | ((uint64_t)(op_right & 0xFF) << 8);
}

uint64_t pack_brancher(int p) {
    return (uint64_t)(p & 0x7F);
}

uint64_t pack_logic(int op_left, int op_right) {
    return ((uint64_t)(op_left  & 0xFF))
         | ((uint64_t)(op_right & 0xFF) << 8);
}

uint64_t pack_slew(int rise, int fall) {
    return ((uint64_t)(rise & 0xFF))
         | ((uint64_t)(fall & 0xFF) << 8);
}

uint64_t pack_burst(int number, int spacing, int div, int jitter, int accel) {
    uint64_t data = 0;
    data |= ((uint64_t)(number   & 0xFF));
    data |= ((uint64_t)(spacing  & 0xFF)) << 8;
    data |= ((uint64_t)((div + 8) & 0xFF)) << 16;
    data |= ((uint64_t)(jitter   & 0xFF)) << 24;
    data |= ((uint64_t)(accel    & 0xFF)) << 32;
    return data;
}

namespace {
constexpr int kAttenOffMaxLevel = 63;  // mirrors ATTENOFF_MAX_LEVEL in vendor AttenuateOffset.h
}

uint64_t pack_atten_off(int offset_left, int offset_right,
                         int level_left, int level_right,
                         bool mix) {
    uint64_t data = 0;
    data |= ((uint64_t)((offset_left  + 256) & 0x1FF));
    data |= ((uint64_t)((offset_right + 256) & 0x1FF)) << 10;
    data |= ((uint64_t)((level_left  + kAttenOffMaxLevel * 2) & 0xFF)) << 19;
    data |= ((uint64_t)((level_right + kAttenOffMaxLevel * 2) & 0xFF)) << 27;
    data |= ((uint64_t)(mix ? 1 : 0)) << 35;
    return data;
}

uint64_t pack_compare(int level) {
    return (uint64_t)(level & 0xFF);
}

uint64_t pack_clock_divider(int div0, int div1, int divmult1_steps, int divmult3_steps) {
    uint64_t data = 0;
    data |= ((uint64_t)((div0          + 32) & 0xFF));
    data |= ((uint64_t)((div1          + 32) & 0xFF)) << 8;
    data |= ((uint64_t)((divmult1_steps + 32) & 0xFF)) << 16;
    data |= ((uint64_t)((divmult3_steps + 32) & 0xFF)) << 24;
    return data;
}

uint64_t pack_clk_to_gate(int width_a, int range_a, int skip_a,
                          int width_b, int range_b, int skip_b) {
    auto pack_side = [](int width, int range, int skip) -> uint64_t {
        uint64_t side = 0;
        side |= ((uint64_t)(width & 0x7F));
        side |= ((uint64_t)((range < 0 ? -range : range) & 0x7F)) << 8;
        side |= ((uint64_t)(range < 0 ? 1 : 0)) << 15;
        side |= ((uint64_t)(skip & 0x7F)) << 16;
        return side;
    };
    uint64_t data = 0;
    data |= pack_side(width_a, range_a, skip_a);
    data |= pack_side(width_b, range_b, skip_b) << 32;
    return data;
}

uint64_t pack_clock_skip(int p0, int p1) {
    uint64_t data = 0;
    data |= ((uint64_t)(p0 & 0x7F));
    data |= ((uint64_t)(p1 & 0x7F)) << 7;
    return data;
}

uint64_t pack_gate_delay(int time_left, int time_right) {
    return ((uint64_t)(time_left  & 0x7FF))
         | ((uint64_t)(time_right & 0x7FF) << 11);
}

uint64_t pack_tlneuron(int w0, int w1, int w2, int threshold) {
    uint64_t data = 0;
    data |= ((uint64_t)((w0 + 9) & 0x1F));
    data |= ((uint64_t)((w1 + 9) & 0x1F)) << 5;
    data |= ((uint64_t)((w2 + 9) & 0x1F)) << 10;
    data |= ((uint64_t)((threshold + 27) & 0x3F)) << 15;
    return data;
}

uint64_t pack_cumulus(int accoperator, int b_constant, int outmode_left, int outmode_right) {
    uint64_t data = 0;
    data |= ((uint64_t)(accoperator   & 0x07));
    data |= ((uint64_t)(b_constant    & 0x0F)) << 3;
    data |= ((uint64_t)(outmode_left  & 0x0F)) << 7;
    // bits 11..12 left as 0 (vendor gap)
    data |= ((uint64_t)(outmode_right & 0x0F)) << 13;
    return data;
}

uint64_t pack_env_follow(int gain0, int gain1, int duck0, int duck1, int speed) {
    uint64_t data = 0;
    data |= ((uint64_t)(gain0        & 0x1F));
    data |= ((uint64_t)(gain1        & 0x1F)) << 5;
    data |= ((uint64_t)(duck0        & 0x01)) << 10;
    data |= ((uint64_t)(duck1        & 0x01)) << 11;
    data |= ((uint64_t)((speed - 1)  & 0x0F)) << 12;
    return data;
}

uint64_t pack_poly_div(int div_enabled, int div0_steps, int div1_steps,
                       int div2_steps, int div3_steps) {
    uint64_t data = 0;
    data |= ((uint64_t)(div_enabled & 0xFF));
    data |= ((uint64_t)(div0_steps  & 0x3F)) <<  8;
    data |= ((uint64_t)(div1_steps  & 0x3F)) << 14;
    data |= ((uint64_t)(div2_steps  & 0x3F)) << 20;
    data |= ((uint64_t)(div3_steps  & 0x3F)) << 26;
    return data;
}

uint64_t pack_rnd_walk(int yClkSrc, int yClkDiv, int range,
                       int step, int smoothness, int cvRange) {
    uint64_t data = 0;
    data |= ((uint64_t)(yClkSrc    & 0x01));
    data |= ((uint64_t)(yClkDiv    & 0x0F)) << 1;
    data |= ((uint64_t)(range      & 0xFF)) << 5;
    data |= ((uint64_t)(step       & 0xFF)) << 13;
    data |= ((uint64_t)(smoothness & 0xFF)) << 21;
    data |= ((uint64_t)(cvRange    & 0x03)) << 29;
    return data;
}

uint64_t pack_rungl_book(int threshold) {
    uint64_t data = 0;
    data |= ((uint64_t)(threshold & 0xFFFF));
    return data;
}

uint64_t pack_schmitt(int low, int high) {
    uint64_t data = 0;
    data |= ((uint64_t)(low  & 0xFFFF));
    data |= ((uint64_t)(high & 0xFFFF)) << 16;
    return data;
}

uint64_t pack_stairs(int steps, int dir, int rand) {
    uint64_t data = 0;
    data |= ((uint64_t)(steps & 0x1F));
    data |= ((uint64_t)(dir   & 0x03)) << 5;
    data |= ((uint64_t)(rand  & 0x01)) << 7;
    return data;
}

uint64_t pack_voltage(int voltage0, int voltage1, int gate0, int gate1) {
    uint64_t data = 0;
    data |= ((uint64_t)((voltage0 + 256) & 0x1FF));        // [0,9)
    // bit 9 left as 0 (vendor gap between voltage[0] and voltage[1])
    data |= ((uint64_t)((voltage1 + 256) & 0x1FF)) << 10;  // [10,9)
    data |= ((uint64_t)(gate0 & 0x1)) << 19;               // [19,1)
    data |= ((uint64_t)(gate1 & 0x1)) << 20;               // [20,1)
    return data;
}

// === BEGIN adeg ===
uint64_t pack_adeg(int attack, int decay) {
    uint64_t data = 0;
    data |= ((uint64_t)(attack & 0xFF));         // [0,8)
    data |= ((uint64_t)(decay  & 0xFF)) << 8;    // [8,8)
    return data;
}
// === END adeg ===

// === BEGIN adsreg ===
uint64_t pack_adsreg(int a0, int d0, int s0, int r0,
                     int a1, int d1, int s1, int r1) {
    uint64_t data = 0;
    data |= ((uint64_t)(a0 & 0xFF))       <<  0;
    data |= ((uint64_t)(d0 & 0xFF))       <<  8;
    data |= ((uint64_t)(s0 & 0xFF))       << 16;
    data |= ((uint64_t)(r0 & 0xFF))       << 24;
    data |= ((uint64_t)(a1 & 0xFF))       << 32;
    data |= ((uint64_t)(d1 & 0xFF))       << 40;
    data |= ((uint64_t)(s1 & 0xFF))       << 48;
    data |= ((uint64_t)(r1 & 0xFF))       << 56;
    return data;
}
// === END adsreg ===

// === BEGIN game_of_life ===
uint64_t pack_game_of_life(int weight) {
    return (uint64_t)(weight & 0x3F);
}
// === END game_of_life ===

// === BEGIN prob_div ===
uint64_t pack_prob_div(int weight_1, int weight_2, int weight_4, int weight_8,
                       int loop_length, int seed) {
    uint64_t data = 0;
    data |= ((uint64_t)(weight_1   & 0x0F));            // [0,4)
    data |= ((uint64_t)(weight_2   & 0x0F)) << 4;       // [4,4)
    data |= ((uint64_t)(weight_4   & 0x0F)) << 8;       // [8,4)
    data |= ((uint64_t)(weight_8   & 0x0F)) << 12;      // [12,4)
    data |= ((uint64_t)(loop_length & 0xFF)) << 16;     // [16,8)
    data |= ((uint64_t)(seed       & 0xFFFF)) << 24;    // [24,16)
    return data;
}
// === END prob_div ===

// === BEGIN shift_gate ===
uint64_t pack_shift_gate(int length_left, int length_right,
                         int trigger_left, int trigger_right,
                         int reg_left) {
    uint64_t data = 0;
    data |= ((uint64_t)((length_left  - 1) & 0x0F));         // [0,4)
    data |= ((uint64_t)((length_right - 1) & 0x0F)) << 4;    // [4,4)
    data |= ((uint64_t)(trigger_left  & 0x01)) << 8;         // [8,1)
    data |= ((uint64_t)(trigger_right & 0x01)) << 9;         // [9,1)
    // bits 10..15 left as 0 (vendor gap; reg[0] starts at bit 16)
    data |= ((uint64_t)(reg_left & 0xFFFF)) << 16;           // [16,16)
    return data;
}
// === END shift_gate ===

// === BEGIN trending ===
uint64_t pack_trending(int assign_left, int assign_right, int sensitivity) {
    uint64_t data = 0;
    data |= ((uint64_t)(assign_left  & 0x0F));        // [0,4)
    data |= ((uint64_t)(assign_right & 0x0F)) << 4;   // [4,4)
    data |= ((uint64_t)(sensitivity  & 0xFF)) << 8;   // [8,8)
    return data;
}
// === END trending ===

// Phase 6 pack helper definitions. Each implementer fills in their slug
// section with the per-applet pack_<applet> body mirroring vendor
// OnDataRequest byte-by-byte.

// === BEGIN vector_lfo ===
uint64_t pack_vector_lfo(int waveform_a, int waveform_b, int pitch_a, int pitch_b, bool modshape) {
    uint64_t data = 0;
    data |= ((uint64_t)(waveform_a & 0x3F));              // [0,6)
    data |= ((uint64_t)(waveform_b & 0x3F)) << 6;         // [6,6)
    data |= ((uint64_t)((int16_t)pitch_a & 0xFFFF)) << 12; // [12,16)
    data |= ((uint64_t)((int16_t)pitch_b & 0xFFFF)) << 28; // [28,16)
    data |= ((uint64_t)(modshape ? 1 : 0)) << 44;          // [44,1)
    return data;
}
// === END vector_lfo ===

// === BEGIN vector_eg ===
// === END vector_eg ===

// === BEGIN vector_mod ===
// === END vector_mod ===

// === BEGIN vector_morph ===
// === END vector_morph ===

// === BEGIN relabi ===
// === END relabi ===

// === BEGIN lower_renz ===
// === END lower_renz ===

// === BEGIN combin8 ===
// === END combin8 ===

// === BEGIN pigeons ===
// === END pigeons ===

// === BEGIN strum ===
// === END strum ===

// === BEGIN shredder ===
// === END shredder ===

// === BEGIN carpeggio ===
// === END carpeggio ===

// === BEGIN squanch ===
// === END squanch ===

// === BEGIN chordinator ===
// === END chordinator ===

// === BEGIN dual_quant ===
// === END dual_quant ===

// === BEGIN enigma_jr ===
// === END enigma_jr ===

// === BEGIN offset_quant ===
// === END offset_quant ===

// === BEGIN multi_scale ===
// === END multi_scale ===

// === BEGIN scale_duet ===
// === END scale_duet ===

// === BEGIN ens_osc_key ===
// === END ens_osc_key ===

// === BEGIN calibr8 ===
// === END calibr8 ===

// === BEGIN reset_clock ===
// === END reset_clock ===

// === BEGIN shuffle ===
// === END shuffle ===

// === BEGIN xfader ===
// === END xfader ===

// === BEGIN scope ===
// === END scope ===

}  // namespace hem_test
