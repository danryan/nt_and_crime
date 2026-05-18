#pragma once
#include <cstdint>

// Slim enum-only header. Safe to include from any TU; pulls in zero
// vendor applet headers. Use this when you only need to refer to applet
// indices (e.g., in tests, in non-canonical translation units). For the
// full factory machinery (applet_factory, applet_enum_strings, applet
// memory bounds), include `HemispheresFactory.h` from the canonical
// applet TU only.

namespace hem_shim {

enum AppletIndex : uint8_t {
    kAppletEmpty = 0,           // sentinel: always index 0 (default selector)
    kAppletADEG,
    kAppletADSREG,
    kAppletAttenuateOffset,
    kAppletBinary,
    kAppletBrancher,
    kAppletBurst,
    kAppletButton,
    kAppletCalculate,
    kAppletClkToGate,
    kAppletClockDivider,
    kAppletClockSkip,
    kAppletCompare,
    kAppletCumulus,
    kAppletEnvFollow,
    kAppletGameOfLife,
    kAppletGateDelay,
    kAppletGatedVCA,
    kAppletLogic,
    kAppletPolyDiv,
    kAppletProbabilityDivider,
    kAppletRndWalk,
    kAppletRunglBook,
    kAppletSchmitt,
    kAppletShiftGate,
    kAppletSlew,
    kAppletStairs,
    kAppletSwitch,
    kAppletTLNeuron,
    kAppletTrending,
    kAppletVoltage,
    kAppletCount
};

}  // namespace hem_shim
