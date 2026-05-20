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

#include "hemispheres_shim.h"

#ifndef APPLET_NAME
#error "APPLET_NAME must be defined to the applet class name"
#endif

// Force the compiler to keep the applet's full vtable + virtual method bodies.
// Without these explicit calls, -Os would eliminate everything not reachable
// from an exported symbol. The probe is exported so the linker keeps it.
// Subclass to access protected SetHelp().
struct SoloProbeApplet : public APPLET_NAME {
    void call_set_help() { this->SetHelp(); }
};

extern "C" __attribute__((visibility("default"), used, noinline))
void measure_solo(uint8_t* sram) {
    HemisphereApplet* a = hem_shim::make_applet<APPLET_NAME>(sram);
    a->BaseStart(LEFT_HEMISPHERE);
    a->Controller();
    a->View();
    a->OnButtonPress();
    a->OnEncoderMove(0);
    a->OnDataRequest();
    a->OnDataReceive(0);
    static_cast<SoloProbeApplet*>(a)->call_set_help();
}
