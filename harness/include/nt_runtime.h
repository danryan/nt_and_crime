#pragma once
#include <cstdint>
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
}
