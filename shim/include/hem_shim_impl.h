#pragma once
// One-stop include that brings in all shim implementations.
// Each applet .cpp must include this exactly once.
#include "../src/cxx_runtime_stubs.cpp"
#include "../src/globals.cpp"
#include "../src/icons.cpp"
#include "../src/graphics.cpp"
// cv_map: bjorklund table + EuclideanFilter/EuclideanPattern.
// bjorklund.h is included via CVInputMap.h; the .cpp provides the data table.
#include "../src/cv_map/bjorklund.cpp"
// quant: braids quantizer + OC scales + HS:: engine.
#include "../src/quant/braids_quantizer.cpp"
#include "../src/quant/OC_scales.cpp"
#include "../src/quant/q_engine.cpp"
