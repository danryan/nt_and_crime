#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <distingnt/api.h>

// Forward declaration to avoid a circular include with plugin_loader.h.
namespace nt { struct LoadedPlugin; }

namespace nt {
void   reset_runtime();
int    num_buses();
int    bus_frame_count();
void   set_bus_frame_count(int frames);
float* bus_pointer(int bus_index_1_based, int numFrames);
float* bus_frames_base();
bool   shape_rasteriser_is_placeholder();

// Algorithm slot registry. The harness hosts exactly one slot.
void   register_algorithm(LoadedPlugin* plugin);
LoadedPlugin* registered_algorithm(int algIdx);

// Gray-out side table query.
bool   is_parameter_grayed_out(int algIdx, int paramIdx);

// Hook called by reset_runtime() to allow plugin_loader to clear its state.
void   reset_plugin_loader();

// Sim-binary hook: when non-null, NT_setParameterFromUi writes one line
// "idx value\n" to this FILE* before calling parameterChanged.
// Pass nullptr to disable logging.
void   set_param_log(FILE* f);

// Model the firmware's common-parameter prefix width. Raises NT_parameterOffset
// so a customUi push-back that omits NT_parameterOffset() lands on the wrong
// global index in the harness, exactly as it would on hardware. reset_runtime
// restores 0.
void   set_parameter_offset(uint32_t offset);

// --- MIDI simulator (Layer 0d, gated on NT_HEM_HOST_SIM) ---
//
// The host stubs for NT_sendMidi2ByteMessage / NT_sendMidi3ByteMessage /
// NT_sendMidiByte record each outgoing message into a capture buffer. Tests
// read it through midi_sent() and reset it through clear_midi_sent().
// reset_runtime() also clears it.
//
// The inject seams send a MIDI message FROM the firmware INTO a loaded plug-in
// by invoking its factory midiMessage / midiRealtime callbacks. They mirror the
// *_test_inject_slot pattern and are no-ops if the plug-in defines no callback.
#if defined(NT_HEM_HOST_SIM)
struct MidiSent {
    uint32_t destination;
    uint8_t  length;  // 1, 2, or 3
    uint8_t  bytes[3];
};
const std::vector<MidiSent>& midi_sent();
void clear_midi_sent();

void send_midi_to_plugin(LoadedPlugin* loaded, uint8_t b0, uint8_t b1, uint8_t b2);
void send_midi_realtime(LoadedPlugin* loaded, uint8_t byte);
#endif
}
