// SoloProbe: instantiates exactly one Hemisphere applet and forces inclusion
// of its vtable + virtual method overrides. Used by tools/measure_per_applet.sh
// to measure per-applet .text contribution.
//
// Build per applet: -DAPPLET_NAME=<class> (e.g., -DAPPLET_NAME=Cumulus).
//
// This is not a deployable plug-in. It produces a relocatable .o whose .text
// reflects only what a single applet brings in (its constructor + virtual
// overrides + transitive shim/vendor code). Compare per applet to find the
// fat ones.

#include <new>
#include "HemisphereApplet.h"
#include "HSUtils.h"

#ifndef APPLET_NAME
#error "APPLET_NAME must be defined to the applet class name"
#endif

// Pull in the specific vendor applet header named by APPLET_NAME. The build
// caller must add -Ivendor/O_C-Phazerville/software/src/applets so the file
// resolves. Vendor applet headers carry no include guards; each solo_probe
// build is a single TU and includes exactly one.
#define SOLO_PROBE_STRINGIFY_INNER(x) #x
#define SOLO_PROBE_STRINGIFY(x) SOLO_PROBE_STRINGIFY_INNER(x)
#define SOLO_PROBE_INCLUDE_X(name) SOLO_PROBE_STRINGIFY(name.h)
#include SOLO_PROBE_INCLUDE_X(APPLET_NAME)

// Force the compiler to keep the applet's full vtable + virtual method bodies.
// Without these explicit calls, -Os would eliminate everything not reachable
// from an exported symbol. The probe is exported so the linker keeps it.
// Subclass to access protected SetHelp().
struct SoloProbeApplet : public APPLET_NAME {
    void call_set_help() { this->SetHelp(); }
};

extern "C" __attribute__((visibility("default"), used, noinline))
void measure_solo(uint8_t* sram) {
    HemisphereApplet* a = new(sram) APPLET_NAME();
    a->BaseStart(LEFT_HEMISPHERE);
    a->Controller();
    a->View();
    a->OnButtonPress();
    a->OnEncoderMove(0);
    a->OnDataRequest();
    a->OnDataReceive(0);
    static_cast<SoloProbeApplet*>(a)->call_set_help();
}
