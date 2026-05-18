#pragma once
#include <cstdint>
#include <distingnt/api.h>
#include "plugin_loader.h"
#include "applet_indices.h"

// Forward declarations only. The full HemispheresInstance / HemisphereApplet
// types live in hemispheres_shim.h, which transitively pulls in vendor
// applet headers whose file-scope globals would cause ODR link errors if
// included into any TU other than the canonical applets/Hemispheres.cpp.
// Test TUs that need typed access to the instance (e.g. for OnDataRequest /
// OnDataReceive) should obtain it through accessor functions defined in the
// canonical TU rather than including the shim header directly.
namespace hem_shim { struct HemispheresInstance; }
class HemisphereApplet;

namespace hem_test {

enum HemSide { LEFT = 0, RIGHT = 1 };
enum HemAxis { GATE_IN, CV_IN, OUT };

// Returns the 1-based NT bus index for the given side/channel/axis under default routing.
int bus_index(HemSide side, int channel, HemAxis axis);

// Writes a single-sample gate pulse (6.0V) at frame_offset on the input gate bus
// for (side, channel). Other frames in that bus stay zero unless the caller wrote them.
void set_gate(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4);

// Writes a sustained gate-high level (6.0V) across all frames of the input gate
// bus for (side, channel). Used when tests need both a rising edge (Clock(side)
// fires once) AND a held high (Gate(side) reads true) within the same step.
// Contrast with `set_gate`, which writes a single-sample pulse for "logical
// clock" scenarios where the rising edge fires but Gate(side) reads false.
void hold_gate(float* bus, HemSide side, int channel, int numFramesBy4);

// Writes a constant CV value (volts) across all frames of the CV input bus for (side, channel).
void set_cv(float* bus, HemSide side, int channel, float volts, int numFramesBy4);

// Reads the output bus for (side, channel) at the given frame and returns true
// if the value is above the gate threshold (>0.5f).
bool read_gate_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4);

// Reads the output bus for (side, channel) at the given frame in volts.
float read_cv_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4);

// Sets the selector parameter for the given side. The next step() will trigger
// the applet swap inside the shim. No external parameterChanged() call needed.
void select_applet(_NT_algorithm* alg, HemSide side, hem_shim::AppletIndex idx);

// Implemented in applets/Hemispheres.cpp where HemispheresInstance fields are
// visible. Takes int (0=LEFT, 1=RIGHT) so the canonical TU need not reference
// hem_test::HemSide.
HemisphereApplet* get_applet_impl(hem_shim::HemispheresInstance* hi, int side);

inline HemisphereApplet* get_applet(hem_shim::HemispheresInstance* hi, HemSide side) {
    return get_applet_impl(hi, static_cast<int>(side));
}

// Typed cast wrapper. _NT_algorithm* must actually point at a HemispheresInstance.
// HemispheresInstance derives from _NT_algorithm at offset 0, so the upcast is
// layout-identical and can be expressed with reinterpret_cast without the
// complete type being visible here.
inline hem_shim::HemispheresInstance* as_instance(_NT_algorithm* alg) {
    return reinterpret_cast<hem_shim::HemispheresInstance*>(alg);
}

// Issues ceil(n_samples / 32) step() calls. Returns total frames advanced
// (a multiple of 32).
int step_n_frames(nt::LoadedPlugin* loaded, _NT_algorithm* alg, float* bus, int n_samples);

// Issues exactly one step() call with the inner-tick budget overridden
// to N. Gates and CV are written ONCE per step() prologue and held
// across all N inner ticks (10x clocked-multiplier rule preserved).
// Used by tick-precise tests that need state evolution independent of
// the default numFrames/3 = 10-tick budget.
void step_n_inner_ticks(nt::LoadedPlugin* loaded, _NT_algorithm* alg,
                        float* bus, int N);

// Vendor int unit conversions. ONE_OCTAVE = 1536 hem units per volt.
int   volts_to_int(float v);
float int_to_volts(int hem_units);

// Writes the shim's hem_rng_state global. Tests call this before any RNG-touching scenario.
void seed_hem_rng(uint32_t seed);

// Mirrors Calculate::OnDataRequest packing: bits [0,8] = op_left, [8,8] = op_right.
uint64_t pack_calculate(int op_left, int op_right);

// Mirrors ClockSkip::OnDataRequest packing: bits [0,7) = p[0], [7,7) = p[1]. No bias.
uint64_t pack_clock_skip(int p0, int p1);

// Mirrors Brancher::OnDataRequest packing: bits [0,7] = p in 0..100.
// Brancher's `choice` field is not serialised by vendor.
uint64_t pack_brancher(int p);

// Mirrors Logic::OnDataRequest packing: bits [0,8] = op_left, [8,8] = op_right.
// Ops: 0=AND, 1=OR, 2=XOR, 3=NAND, 4=NOR, 5=XNOR, 6=CV-controlled.
uint64_t pack_logic(int op_left, int op_right);

// Mirrors Slew::OnDataRequest: bits [0,8] = rise (0..HEM_SLEW_MAX_VALUE), [8,8] = fall.
uint64_t pack_slew(int rise, int fall);

// Mirrors Burst::OnDataRequest packing (40 bits):
//   bits [0, 8)  = number  (1..HEM_BURST_NUMBER_MAX)
//   bits [8, 8)  = spacing (HEM_BURST_SPACING_MIN..HEM_BURST_SPACING_MAX, in ms)
//   bits [16,8)  = div + 8 (biased; div is -HEM_BURST_CLOCKDIV_MAX..HEM_BURST_CLOCKDIV_MAX)
//   bits [24,8)  = jitter  (0..HEM_BURST_JITTER_MAX)
//   bits [32,8)  = accel   (-HEM_BURST_ACCEL_MAX..HEM_BURST_ACCEL_MAX, stored 2's-complement in 8 bits)
uint64_t pack_burst(int number, int spacing, int div, int jitter, int accel);

// Mirrors AttenuateOffset::OnDataRequest packing (36 bits):
//   bits [0, 9)  = offset[0] + 256   (biased, semitones, range +/- HEMISPHERE_MAX_CV/ATTENOFF_INCREMENTS)
//   bits [10,19) = offset[1] + 256   (bit 9 is an unused gap)
//   bits [19,27) = level[0] + ATTENOFF_MAX_LEVEL*2  (biased, range +/- ATTENOFF_MAX_LEVEL*2)
//   bits [27,35) = level[1] + ATTENOFF_MAX_LEVEL*2
//   bit  35      = mix flag
// ATTENOFF_MAX_LEVEL is 63 in vendor AttenuateOffset.h, so the level bias is 126
// and the encoded level field is 8 bits.
uint64_t pack_atten_off(int offset_left, int offset_right,
                         int level_left, int level_right,
                         bool mix);

// Mirrors Compare::OnDataRequest packing: bits [0,8] = level in 0..HEM_COMPARE_MAX_VALUE.
// HEM_COMPARE_MAX_VALUE is 255 in vendor Compare.h; default level is 128.
uint64_t pack_compare(int level);

// Mirrors ClockDivider::OnDataRequest packing (32 bits):
//   bits [0, 8)  = div[0] + 32   (biased; positive=divide, negative=multiply, zero=mute)
//   bits [8, 8)  = div[1] + 32
//   bits [16, 8) = divmult[1].steps + 32  (second-stage multiplier for ch0)
//   bits [24, 8) = divmult[3].steps + 32  (second-stage multiplier for ch1)
// Both div[i] and divmult[1+i*2].steps are biased +32 on pack.
uint64_t pack_clock_divider(int div0, int div1, int divmult1_steps, int divmult3_steps);

// Mirrors ClkToGate::OnDataRequest: per side i in {0,1}:
//   width[i] at (i*32+0, 7)
//   abs(range[i]) at (i*32+8, 7)
//   range[i] sign at (i*32+15, 1)
//   skip[i] at (i*32+16, 7)
uint64_t pack_clk_to_gate(int width_a, int range_a, int skip_a,
                          int width_b, int range_b, int skip_b);

// Mirrors GateDelay::OnDataRequest: bits [0,11] = time[0], [11,11] = time[1].
// Times are in milliseconds, valid range 0..2000 (clamped by vendor).
uint64_t pack_gate_delay(int time_left, int time_right);

// Mirrors TLNeuron::OnDataRequest: per-dendrite weight (5 bits, +9 bias) at
// offsets 0/5/10; threshold (6 bits, +27 bias) at offset 15.
uint64_t pack_tlneuron(int w0, int w1, int w2, int threshold);

// Mirrors Cumulus::OnDataRequest packing (17 bits used):
//   bits [0, 3)  = accoperator (ADD=0, SUB=1, MULADD1=2, XOR_ROTL=3, SUB_ROTR=4)
//   bits [3, 4)  = b_constant  (0..ACC_MAX_B=15)
//   bits [7, 4)  = outmode[0]  (0..7; vendor constrains to 0..7 on receive)
//   bits [11, 2) = UNUSED gap (must be 0 on pack)
//   bits [13, 4) = outmode[1]  (0..7; vendor constrains to 0..7 on receive)
// IMPORTANT: bits 11..12 are unused in vendor packing; pack_cumulus explicitly
// zeros them to avoid stale state leaking through preset round-trip.
uint64_t pack_cumulus(int accoperator, int b_constant, int outmode_left, int outmode_right);

// Mirrors EnvFollow::OnDataRequest packing (16 bits used):
//   bits [0, 5)  = gain[0]   (1..31)
//   bits [5, 5)  = gain[1]   (1..31)
//   bits [10, 1) = duck[0]   (0 or 1)
//   bits [11, 1) = duck[1]   (0 or 1)
//   bits [12, 4) = speed - 1 (biased; caller passes natural speed 1..16)
uint64_t pack_env_follow(int gain0, int gain1, int duck0, int duck1, int speed);
// Mirrors PolyDiv::OnDataRequest (32 bits):
//   bits [0, 8)  = div_enabled  (8-bit bitmask; bits 0-3 = dividers for Out A,
//                                bits 4-7 = dividers for Out B)
//   bits [8, 6)  = divider[0].steps  (6-bit, 0..63)
//   bits [14,6)  = divider[1].steps
//   bits [20,6)  = divider[2].steps
//   bits [26,6)  = divider[3].steps
// Vendor ForAllChannels iterates i=0..3; all four step fields must be provided.
uint64_t pack_poly_div(int div_enabled, int div0_steps, int div1_steps,
                       int div2_steps, int div3_steps);
// Mirrors RndWalk::OnDataRequest (31 bits, no bias on any field):
//   bits [0,  1)  = yClkSrc    (0=TR1, 1=TR2)
//   bits [1,  4)  = yClkDiv    (1..32, 4 bits stored)
//   bits [5,  8)  = range      (0..255)
//   bits [13, 8)  = step       (0..255)
//   bits [21, 8)  = smoothness (0..255)
//   bits [29, 2)  = cvRange    (0..3)
uint64_t pack_rnd_walk(int yClkSrc, int yClkDiv, int range,
                       int step, int smoothness, int cvRange);
// Mirrors RunglBook::OnDataRequest packing (16 bits):
//   bits [0, 16) = threshold  (ONE_OCTAVE..ONE_OCTAVE*5 = 1536..7680; no bias)
// Start() default: threshold = ONE_OCTAVE * 2 = 3072.
uint64_t pack_rungl_book(int threshold);
// Mirrors Schmitt::OnDataRequest packing (32 bits used):
//   bits [0, 16)  = low  threshold (vendor int CV units, no bias)
//   bits [16, 16) = high threshold (vendor int CV units, no bias)
// Both fields are uint16_t in the vendor source; valid range 64..HEMISPHERE_MAX_CV.
// Default: low=3200, high=3968.
uint64_t pack_schmitt(int low, int high);
// Mirrors Stairs::OnDataRequest packing (8 bits):
//   bits [0, 5)  = steps  (0..31; default 1)
//   bits [5, 2)  = dir    (0=up, 1=up-down, 2=down; default 0)
//   bits [7, 1)  = rand   (0=off, 1=on; default 0)
// No bias fields. All fields stored without offset.
uint64_t pack_stairs(int steps, int dir, int rand);
// Mirrors Voltage::OnDataRequest packing (21 bits used, 1-bit gap):
//   bits [0, 9)  = voltage[0] + 256  (biased; semitone units, VOLTAGE_INCREMENTS=128)
//   bit  9       = UNUSED gap (must be 0; not written by vendor Pack calls)
//   bits [10, 9) = voltage[1] + 256  (biased; same units)
//   bit  19      = gate[0]            (0 = normally-on, 1 = normally-off)
//   bit  20      = gate[1]
// VOLTAGE_MAX = HEMISPHERE_MAX_CV / VOLTAGE_INCREMENTS = 72 (6V).
// VOLTAGE_MIN = HEMISPHERE_MIN_CV / VOLTAGE_INCREMENTS = -72 (-6V).
// Defaults after Start(): voltage[0]=72, voltage[1]=-72, gate[0]=0, gate[1]=0.
uint64_t pack_voltage(int voltage0, int voltage1, int gate0, int gate1);

// === BEGIN adeg ===
// Mirrors ADEG::OnDataRequest packing (16 bits):
//   bits [0,8) = attack (0..HEM_ADEG_MAX_VALUE=255)
//   bits [8,8) = decay
// No bias.
uint64_t pack_adeg(int attack, int decay);
// === END adeg ===

// === BEGIN adsreg ===
// Mirrors ADSREG::OnDataRequest packing (64 bits):
// Per channel ch in {0,1}:
//   bits [ch*32 +  0, 8) = setting[ATTACK]   (1..STAGE_MAX_VALUE=255)
//   bits [ch*32 +  8, 8) = setting[DECAY]
//   bits [ch*32 + 16, 8) = setting[SUSTAIN]
//   bits [ch*32 + 24, 8) = setting[RELEASE]
// No bias. OnDataReceive constrains each field to [1, STAGE_MAX_VALUE] for
// attack/decay/release; sustain stays [0, STAGE_MAX_VALUE].
uint64_t pack_adsreg(int a0, int d0, int s0, int r0,
                     int a1, int d1, int s1, int r1);
// === END adsreg ===

// === BEGIN game_of_life ===
// Mirrors GameOfLife::OnDataRequest packing (6 bits):
//   bits [0,6) = weight  (0..63; controls global density divisor)
// No bias. Board state is not serialised; vendor relies on Start() reseed.
uint64_t pack_game_of_life(int weight);
// === END game_of_life ===

// === BEGIN prob_div ===
// Mirrors ProbabilityDivider::OnDataRequest packing (40 bits):
//   bits [0,4)   = weight_1   (0..MAX_WEIGHT=15)
//   bits [4,4)   = weight_2
//   bits [8,4)   = weight_4
//   bits [12,4)  = weight_8
//   bits [16,8)  = loop_length (0..MAX_LOOP_LENGTH=32)
//   bits [24,16) = loop_linker.GetSeed() (16-bit seed)
// No bias on any field. loop_step / loop_index / skip_steps are runtime-only.
uint64_t pack_prob_div(int weight_1, int weight_2, int weight_4, int weight_8,
                       int loop_length, int seed);
// === END prob_div ===

// === BEGIN shift_gate ===
// Mirrors ShiftGate::OnDataRequest packing (32 bits used):
//   bits [0,4)  = length[0] - 1  (vendor stores length-1; receive adds 1 back)
//   bits [4,4)  = length[1] - 1
//   bits [8,1)  = trigger[0]     (0 = Gate, 1 = Trigger)
//   bits [9,1)  = trigger[1]
//   bits [16,16) = reg[0]        (only ch0 register persisted)
// reg[1] is not serialised; tests cannot inject ch1 register state via pack.
uint64_t pack_shift_gate(int length_left, int length_right,
                         int trigger_left, int trigger_right,
                         int reg_left);
// === END shift_gate ===

// === BEGIN trending ===
// Mirrors Trending::OnDataRequest packing (16 bits used):
//   bits [0,4)  = assign[0]    (0..5: Rising, Falling, Moving, Steady, ChgState, ChgValue)
//   bits [4,4)  = assign[1]
//   bits [8,8)  = sensitivity  (4..TRENDING_MAX_SENS=124)
// No bias on any field.
uint64_t pack_trending(int assign_left, int assign_right, int sensitivity);
// === END trending ===

}  // namespace hem_test
