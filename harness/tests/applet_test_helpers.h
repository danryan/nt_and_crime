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

// Vendor int unit conversions. ONE_OCTAVE = 1536 hem units per volt.
int   volts_to_int(float v);
float int_to_volts(int hem_units);

// Writes the shim's hem_rng_state global. Tests call this before any RNG-touching scenario.
void seed_hem_rng(uint32_t seed);

// Mirrors Calculate::OnDataRequest packing: bits [0,8] = op_left, [8,8] = op_right.
uint64_t pack_calculate(int op_left, int op_right);

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

}  // namespace hem_test
