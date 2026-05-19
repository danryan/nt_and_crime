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
uint64_t pack_vector_eg(int waveform0, int waveform1,
                        int freq0, int freq1, int modshape) {
    uint64_t data = 0;
    data |= ((uint64_t)(waveform0 & 0x3F));          // [0,6)
    data |= ((uint64_t)(waveform1 & 0x3F)) << 6;     // [6,6)
    data |= ((uint64_t)(freq0    & 0x3FF)) << 12;    // [12,10)
    data |= ((uint64_t)(freq1    & 0x3FF)) << 22;    // [22,10)
    data |= ((uint64_t)(modshape & 0x1))  << 32;     // [32,1)
    return data;
}
// === END vector_eg ===

// === BEGIN vector_mod ===
uint64_t pack_vector_mod(int wf0, int wf1, int freq0, int freq1) {
    uint64_t data = 0;
    data |= ((uint64_t)(wf0   & 0x3F));         // [0,6)
    data |= ((uint64_t)(wf1   & 0x3F)) << 6;    // [6,6)
    data |= ((uint64_t)(freq0 & 0x3FF)) << 12;  // [12,10)
    data |= ((uint64_t)(freq1 & 0x3FF)) << 22;  // [22,10)
    return data;
}
// === END vector_mod ===

// === BEGIN vector_morph ===
uint64_t pack_vector_morph(int waveform0, int waveform1, int phase0, int phase1, int linked) {
    uint64_t data = 0;
    data |= ((uint64_t)(waveform0 & 0x3F));          // [0,6)
    data |= ((uint64_t)(waveform1 & 0x3F)) << 6;     // [6,6)
    data |= ((uint64_t)(phase0    & 0x1FF)) << 12;   // [12,9)
    data |= ((uint64_t)(phase1    & 0x1FF)) << 21;   // [21,9)
    data |= ((uint64_t)(linked    & 0x01))  << 30;   // [30,1)
    return data;
}
// === END vector_morph ===

// === BEGIN relabi ===
uint64_t pack_relabi(
    int freq0, int freq1, int freq2,
    int xmod0, int xmod1, int xmod2,
    int phase0, int phase1, int phase2,
    int thresh0, int thresh1, int thresh2,
    int freq_mul, int freq_div,
    int out0, int out1, int out2, int out3)
{
    uint64_t data = 0;
    data |= ((uint64_t)(freq0   & 0x3F));            // bits [0,6)
    data |= ((uint64_t)(freq1   & 0x3F)) << 6;       // bits [6,6)
    data |= ((uint64_t)(freq2   & 0x3F)) << 12;      // bits [12,6)
    data |= ((uint64_t)(xmod0   & 0x07)) << 18;      // bits [18,3)
    data |= ((uint64_t)(xmod1   & 0x07)) << 21;      // bits [21,3)
    data |= ((uint64_t)(xmod2   & 0x07)) << 24;      // bits [24,3)
    data |= ((uint64_t)(phase0  & 0x07)) << 27;      // bits [27,3)
    data |= ((uint64_t)(phase1  & 0x07)) << 30;      // bits [30,3)
    data |= ((uint64_t)(phase2  & 0x07)) << 33;      // bits [33,3)
    data |= ((uint64_t)(thresh0 & 0x07)) << 36;      // bits [36,3)
    data |= ((uint64_t)(thresh1 & 0x07)) << 39;      // bits [39,3)
    data |= ((uint64_t)(thresh2 & 0x07)) << 42;      // bits [42,3)
    data |= ((uint64_t)(freq_mul & 0x07)) << 45;     // bits [45,3)
    data |= ((uint64_t)(freq_div & 0x07)) << 48;     // bits [48,3)
    data |= ((uint64_t)(out0    & 0x07)) << 51;      // bits [51,3)
    data |= ((uint64_t)(out1    & 0x07)) << 54;      // bits [54,3)
    data |= ((uint64_t)(out2    & 0x07)) << 57;      // bits [57,3)
    data |= ((uint64_t)(out3    & 0x07)) << 60;      // bits [60,3)
    return data;
}
// === END relabi ===

// === BEGIN lower_renz ===
// freq: bits 0-7, rho: bits 8-15. Mirrors LowerRenz::OnDataRequest exactly.
uint64_t pack_lower_renz(int freq, int rho) {
    uint64_t data = 0;
    data |= ((uint64_t)(freq & 0xFF)) << 0;
    data |= ((uint64_t)(rho  & 0xFF)) << 8;
    return data;
}
// === END lower_renz ===

// === BEGIN combin8 ===
uint64_t pack_combin8(int src00, int att00,
                      int src01, int att01,
                      int src10, int att10,
                      int src11, int att11) {
    // Each CVInputMap packs as 16 bits: low byte = source, high byte = attenuversion
    // (stored as uint8_t to preserve negative values via reinterpret).
    auto pack_one = [](int src, int att) -> uint64_t {
        return ((uint64_t)(src & 0xFF)) | ((uint64_t)((uint8_t)att) << 8);
    };
    uint64_t data = 0;
    data |= pack_one(src00, att00);           // bits [0,16)
    data |= pack_one(src01, att01) << 16;     // bits [16,16)
    data |= pack_one(src10, att10) << 32;     // bits [32,16)
    data |= pack_one(src11, att11) << 48;     // bits [48,16)
    return data;
}
// === END combin8 ===

// === BEGIN pigeons ===
uint64_t pack_pigeons(int val0_0, int val0_1, int mod0,
                      int val1_0, int val1_1, int mod1,
                      int qsel0, int qsel1) {
    uint64_t data = 0;
    data |= ((uint64_t)(val0_0 & 0x3F));              // [0,6)
    data |= ((uint64_t)(val0_1 & 0x3F)) << 6;         // [6,6)
    data |= ((uint64_t)((mod0 - 1) & 0x3F)) << 12;    // [12,6) stored as mod-1
    data |= ((uint64_t)(val1_0 & 0x3F)) << 18;        // [18,6)
    data |= ((uint64_t)(val1_1 & 0x3F)) << 24;        // [24,6)
    data |= ((uint64_t)((mod1 - 1) & 0x3F)) << 30;    // [30,6) stored as mod-1
    data |= ((uint64_t)(qsel0 & 0x0F)) << 36;         // [36,4)
    // bits [40,4) = gap; left as 0
    data |= ((uint64_t)(qsel1 & 0x0F)) << 44;         // [44,4)
    return data;
}
// === END pigeons ===

// === BEGIN strum ===
uint64_t pack_strum(int qselect, int spacing, int length,
                    int iv0, int iv1, int iv2, int iv3, int iv4, int iv5,
                    int stepmode, int qmod) {
    static const int MIN_INTERVAL = -12;
    uint64_t data = 0;
    data |= ((uint64_t)(qselect  & 0x0F));                   // [0,4)
    // bits [4,8) left as 0 (vendor gap before spacing at bit 12)
    data |= ((uint64_t)(spacing  & 0x1FF)) << 12;            // [12,9)
    data |= ((uint64_t)(length   & 0x0F))  << 21;            // [21,4)
    data |= ((uint64_t)((iv0 - MIN_INTERVAL) & 0x3F)) << 25; // [25,6)
    data |= ((uint64_t)((iv1 - MIN_INTERVAL) & 0x3F)) << 31; // [31,6)
    data |= ((uint64_t)((iv2 - MIN_INTERVAL) & 0x3F)) << 37; // [37,6)
    data |= ((uint64_t)((iv3 - MIN_INTERVAL) & 0x3F)) << 43; // [43,6)
    data |= ((uint64_t)((iv4 - MIN_INTERVAL) & 0x3F)) << 49; // [49,6)
    data |= ((uint64_t)((iv5 - MIN_INTERVAL) & 0x3F)) << 55; // [55,6)
    data |= ((uint64_t)(stepmode & 0x01))              << 61; // [61,1)
    data |= ((uint64_t)(qmod     & 0x01))              << 62; // [62,1)
    return data;
}
// === END strum ===

// === BEGIN shredder ===
uint64_t pack_shredder(int range0, int bipolar0, int shred_on_reset0,
                       int range1, int bipolar1, int shred_on_reset1,
                       int quant_channels, int scale,
                       int seed0, int seed1) {
    uint64_t data = 0;
    data |= ((uint64_t)(range0           & 0x0F));         // [0,4)
    data |= ((uint64_t)(bipolar0         & 0x01)) << 4;    // [4,1)
    data |= ((uint64_t)(shred_on_reset0  & 0x01)) << 5;    // [5,1)
    // bits 6..7: unused gap, left as 0
    data |= ((uint64_t)(range1           & 0x0F)) << 8;    // [8,4)
    data |= ((uint64_t)(bipolar1         & 0x01)) << 12;   // [12,1)
    data |= ((uint64_t)(shred_on_reset1  & 0x01)) << 13;   // [13,1)
    // bits 14..15: unused gap, left as 0
    data |= ((uint64_t)(quant_channels   & 0xFF)) << 16;   // [16,8)
    data |= ((uint64_t)(scale            & 0xFF)) << 24;   // [24,8)
    data |= ((uint64_t)(seed0            & 0xFFFF)) << 32; // [32,16)
    data |= ((uint64_t)(seed1            & 0xFFFF)) << 48; // [48,16)
    return data;
}
// === END shredder ===

// === BEGIN carpeggio ===
uint64_t pack_carpeggio(int sel_chord, int transpose) {
    uint64_t data = 0;
    data |= (static_cast<uint64_t>(sel_chord & 0xFF) << 0);
    data |= (static_cast<uint64_t>((transpose + 24) & 0xFF) << 8);
    return data;
}
// === END carpeggio ===

// === BEGIN squanch ===
uint64_t pack_squanch(int scale, int shift0, int shift1,
                      int root, int note_wrap0, int note_wrap1) {
    uint64_t data = 0;
    data |= (uint64_t)(scale            & 0xFF);
    data |= (uint64_t)((shift0 + 48)   & 0xFF) << 8;
    data |= (uint64_t)((shift1 + 48)   & 0xFF) << 16;
    data |= (uint64_t)(root             & 0x0F) << 24;
    data |= (uint64_t)(note_wrap0       & 0x3F) << 28;
    data |= (uint64_t)(note_wrap1       & 0x3F) << 34;
    return data;
}
// === END squanch ===

// === BEGIN chordinator ===
uint64_t pack_chordinator(int scale, int root_note, int chord_mask) {
    uint64_t data = 0;
    data |= ((uint64_t)(scale      & 0xFF)) << 0;   // [0, 8)
    data |= ((uint64_t)(root_note  & 0x0F)) << 8;   // [8, 4)
    data |= ((uint64_t)(chord_mask & 0xFFFF)) << 12; // [12,16)
    return data;
}
// === END chordinator ===

// === BEGIN dual_quant ===
uint64_t pack_dual_quant(int scale0, int scale1, int root0, int root1) {
    uint64_t data = 0;
    data |= ((uint64_t)(scale0 & 0xFF));
    data |= ((uint64_t)(scale1 & 0xFF)) << 8;
    data |= ((uint64_t)(root0  & 0x0F)) << 16;
    data |= ((uint64_t)(root1  & 0x0F)) << 20;
    return data;
}
// === END dual_quant ===

// === BEGIN enigma_jr ===
uint64_t pack_enigma_jr(int p, int type0, int type1, int tm_index) {
    uint64_t data = 0;
    data |= ((uint64_t)(p        & 0x7F));            // [0,7)
    data |= ((uint64_t)(type0    & 0x0F)) << 7;       // [7,4)
    data |= ((uint64_t)(type1    & 0x0F)) << 11;      // [11,4)
    data |= ((uint64_t)(tm_index & 0xFFFF)) << 15;    // [15,16)
    return data;
}
// === END enigma_jr ===

// === BEGIN offset_quant ===
uint64_t pack_offset_quant(int range0, int range1, int scale0, int scale1,
                            int root0, int root1) {
    uint64_t data = 0;
    data |= ((uint64_t)(range0 & 0x7));          // bits [0,3)
    data |= ((uint64_t)(range1 & 0x7)) << 3;     // bits [3,3)
    data |= ((uint64_t)(scale0 & 0xFF)) << 6;    // bits [6,8)
    data |= ((uint64_t)(scale1 & 0xFF)) << 14;   // bits [14,8)
    data |= ((uint64_t)(root0 & 0xF)) << 22;     // bits [22,4)
    data |= ((uint64_t)(root1 & 0xF)) << 26;     // bits [26,4)
    return data;
}
// === END offset_quant ===

// === BEGIN multi_scale ===
uint64_t pack_multi_scale(int mask0, int mask1, int mask2, int mask3) {
    uint64_t data = 0;
    data |= ((uint64_t)(mask0 & 0x0FFF));           // [0,12)
    data |= ((uint64_t)(mask1 & 0x0FFF)) << 12;     // [12,12)
    data |= ((uint64_t)(mask2 & 0x0FFF)) << 24;     // [24,12)
    data |= ((uint64_t)(mask3 & 0x0FFF)) << 36;     // [36,12)
    return data;
}
// === END multi_scale ===

// === BEGIN scale_duet ===
uint64_t pack_scale_duet(int mask0, int mask1) {
    uint64_t data = 0;
    data |= ((uint64_t)(mask0 & 0xFFF));         // [0,12)
    data |= ((uint64_t)(mask1 & 0xFFF)) << 12;   // [12,12)
    return data;
}
// === END scale_duet ===

// === BEGIN ens_osc_key ===
uint64_t pack_ens_osc_key(int scale, int octave, int voltage_maj,
                           int voltage_min, int voltage_dim,
                           int voltage_no_match, int root) {
    uint64_t data = 0;
    data |= ((uint64_t)(scale           & 0xFF));          // [0,8)
    data |= ((uint64_t)((octave + 5)    & 0x0F)) << 8;    // [8,4)  bias: +5
    data |= ((uint64_t)(voltage_maj     & 0x0F)) << 12;   // [12,4)
    data |= ((uint64_t)(voltage_min     & 0x0F)) << 16;   // [16,4)
    data |= ((uint64_t)(voltage_dim     & 0x0F)) << 20;   // [20,4)
    data |= ((uint64_t)(voltage_no_match & 0x0F)) << 24;  // [24,4)
    data |= ((uint64_t)(root            & 0x0F)) << 28;   // [28,4)
    return data;
}
// === END ens_osc_key ===

// === BEGIN calibr8 ===
uint64_t pack_calibr8(int scale0, int scale1,
                      int offset0, int offset1,
                      int transpose0, int transpose1) {
    uint64_t data = 0;
    data |= ((uint64_t)((scale0 + 500) & 0x3FF));         // [0,10)
    data |= ((uint64_t)((scale1 + 500) & 0x3FF)) << 10;   // [10,10)
    data |= ((uint64_t)((offset0 + 100) & 0xFF)) << 20;   // [20,8)
    data |= ((uint64_t)((offset1 + 100) & 0xFF)) << 28;   // [28,8)
    data |= ((uint64_t)((transpose0 + 36) & 0x7F)) << 36; // [36,7)
    data |= ((uint64_t)((transpose1 + 36) & 0x7F)) << 43; // [43,7)
    return data;
}
// === END calibr8 ===

// === BEGIN reset_clock ===
uint64_t pack_reset_clock(int length, int offset, int spacing) {
    uint64_t data = 0;
    data |= ((uint64_t)((length - 1) & 0x1F));          // bits [0,5)
    data |= ((uint64_t)(offset       & 0x1F)) << 5;     // bits [5,5)
    data |= ((uint64_t)(spacing      & 0x7F)) << 10;    // bits [10,7)
    return data;
}
// === END reset_clock ===

// === BEGIN shuffle ===
uint64_t pack_shuffle(int delay0, int delay1) {
    return ((uint64_t)(delay0 & 0x7F))
         | ((uint64_t)(delay1 & 0x7F) << 7);
}
// === END shuffle ===

// === BEGIN xfader ===
uint64_t pack_xfader(int balance, int rate, int center, int center_reset_enable) {
    uint64_t data = 0;
    data |= ((uint64_t)(balance              & 0xFF));        // [0,8)
    data |= ((uint64_t)(rate                 & 0xFFFF)) << 8; // [8,16)
    data |= ((uint64_t)(center               & 0xFF)) << 24;  // [24,8)
    data |= ((uint64_t)(center_reset_enable  & 0x01)) << 32;  // [32,1)
    return data;
}
// === END xfader ===

// === BEGIN scope ===
// === END scope ===

}  // namespace hem_test
