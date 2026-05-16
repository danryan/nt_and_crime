#pragma once
#include <cstdint>
#include <distingnt/api.h>

namespace nt {
void   reset_runtime();
int    num_buses();
int    bus_frame_count();
void   set_bus_frame_count(int frames);
float* bus_pointer(int bus_index_1_based, int numFrames);
float* bus_frames_base();
bool   shape_rasteriser_is_placeholder();
}
