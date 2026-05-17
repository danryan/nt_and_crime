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

}  // namespace hem_test
