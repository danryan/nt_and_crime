#pragma once
// One-stop include that brings in all shim implementations.
// Each applet .cpp must include this exactly once.
#include "../src/globals.cpp"
#include "../src/icons.cpp"
#include "../src/graphics.cpp"
// Euclidean pattern table + EuclideanFilter/EuclideanPattern implementations.
// bjorklund.h is included via CVInputMap.h; the .cpp provides the data table.
#include "../src/cv_map/bjorklund.cpp"
