#pragma once
// One-stop include for the O_C app TUs that brings in all OC-specific shim
// implementations. Each plugins/apps/<APP>.cpp must include this exactly once
// (via _per_app_runtime.h on ARM builds). Mirrors hem_shim_impl.h but pulls
// the O_C component set instead of the Hemisphere-coupled set; specifically
// it does NOT include HemisphereApplet.h or HSIOFrame.h. (HSClockManager.h IS
// pulled transitively: globals.cpp includes it to define the clock_m global.)
//
// Order matters: cxx_runtime_stubs.cpp first (operator new / __dso_handle
// stubs the rest may reach), then globals.cpp (DAC_CHANNEL_* objects, shared
// string tables, OC::CORE::ticks), then graphics + icons (rendering deps),
// then the OC-only sources that depend on them.
#include "../src/cxx_runtime_stubs.cpp"
#include "../src/globals.cpp"
#include "../src/icons.cpp"
#include "../src/graphics.cpp"
// cv_map (bjorklund) is shim-owned and reused by Harrington 1200.
#include "../src/cv_map/bjorklund.cpp"
// quant subsystem provides SemitoneQuantizer (used by Harrington 1200) and
// the OC scales tables that several full-screen apps reach.
#include "../src/quant/braids_quantizer.cpp"
#include "../src/quant/OC_scales.cpp"
#include "../src/quant/q_engine.cpp"
// OC-only sources: I/O backing (ADC_CHANNEL_* objects, input/trigger store)
// and the hand-ported menu widgets. Both are NOT part of SHIM_CORE_SRCS so
// they never aggregate into a Hemisphere applet TU.
#include "../src/oc/io.cpp"
#include "../src/oc/menus.cpp"
