// Hemispheres2: secondary applet host for the 5 largest applets that don't
// fit in the primary Hemispheres.o under the NT firmware's per-plug-in .text
// budget (empirically ~82KB). Compiled with -DHEMI_VARIANT=2 so the shared
// HemispheresFactory.h registers only Relabi, Shredder, EnsOscKey,
// VectorLFO, Strum. All other slots are stubbed to Empty.
//
// Same host shape, same parameter layout, same shim machinery. Distinct GUID
// and name string so the firmware lists it as a separate plug-in. Host tests
// build only the primary (variant 0) so the test seam `get_applet_impl`
// lives in Hemispheres.cpp; this TU does NOT redefine it.
#include "hemispheres_shim.h"

NT_HEMISPHERES_PLUGIN("hmi2", "Hemispheres2",
                     "Phazerville Hemisphere pair (secondary applet set)")
