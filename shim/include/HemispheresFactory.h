#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
#include "applet_indices.h"
#include "HemisphereApplet.h"
#include "PackingUtils.h"

// Pre-include Phase 5 dep prereqs. These define DMAMEM (no-op on host and
// NT), InterpLinear16 (used by vendor HSVectorOscillator.h), and ensure
// shim's util_math.h is in scope before vendor headers pull their own
// util_math.h via relative include. Shim util_math.h pre-defines the
// vendor traditional include guard UTIL_MATH_H_ so vendor's body
// becomes a no-op once shim's is in scope.
#include "vector_osc/vec_osc_prereqs.h"
// Pre-include shim copies of Phase 5 dep headers that carry traditional
// include guards (HS_VECTOR_OSCILLATOR, WAVEFORM_MANAGER_H,
// STREAMS_LORENZ_GENERATOR_H_). When a vendor applet later does a relative
// `../<dep>.h` include, the guard is already defined so vendor's copy of
// the dep is skipped. Eliminates duplicate-symbol link failures when both
// shim and vendor copies would otherwise reach the same TU.
//
// NOT pre-included: HSRelabiManager.h and waveform_library.h. The vendor
// copies lack include guards, so the only way to keep symbol counts at one
// per TU is to let the vendor relative include be the single source.
// dep tests (test_dep_vec_osc.cpp) include the shim copies and do not link
// against Hemispheres.host.o, so the test binary sees only the shim copy.
//
// Suppress -Wunused-function for the WaveformManager static editor helpers
// (DeleteSegmentFromWaveformAtSegmentIndex, AddWaveform, AddSegmentTo...,
// Update, SegmentsRemaining, Validate). They are part of the dep surface
// the applets do not invoke at compile time.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "vector_osc/HSVectorOscillator.h"
#include "vector_osc/WaveformManager.h"
#include "lorenz/streams_lorenz_generator.h"
#pragma GCC diagnostic pop

#include "Empty.h"
#include "Logic.h"
#include "AttenuateOffset.h"
#include "Brancher.h"
#include "Slew.h"
#include "TLNeuron.h"
#include "Calculate.h"
#include "Burst.h"
#include "Button.h"
#include "ClkToGate.h"
#include "ClockDivider.h"
#include "ClockSkip.h"
#include "Compare.h"
#include "Cumulus.h"
#include "EnvFollow.h"
#include "GateDelay.h"
#include "GatedVCA.h"
#include "PolyDiv.h"
#include "RndWalk.h"
#include "RunglBook.h"
#include "Schmitt.h"
#include "Stairs.h"
#include "Switch.h"
#include "Voltage.h"
// Phase 4 additions
#include "ADEG.h"
#include "ADSREG.h"
#include "Binary.h"
#include "GameOfLife.h"
#include "ProbabilityDivider.h"
#include "ShiftGate.h"
#include "Trending.h"
// Phase 6 additions: Class A (vendor-dep)
#include "VectorLFO.h"
#include "VectorEG.h"
#include "VectorMod.h"
#include "VectorMorph.h"
#include "Relabi.h"
#include "LowerRenz.h"
#include "Combin8.h"
// Phase 6 additions: Class B (quantizer)
#include "Pigeons.h"
#include "Strum.h"
#include "Shredder.h"
#include "Carpeggio.h"
#include "Squanch.h"
#include "Chordinator.h"
#include "DualQuant.h"
#include "EnigmaJr.h"
#include "OffsetQuant.h"
#include "MultiScale.h"
#include "ScaleDuet.h"
#include "EnsOscKey.h"
#include "Calibr8.h"
// Phase 6 additions: Class C (helper-using)
#include "ResetClock.h"
#include "Shuffle.h"
#include "Xfader.h"
#include "Scope.h"
// Phase 6 additions: Class D (clock-mgr)
#include "Metronome.h"

namespace hem_shim {

inline const char* const* applet_enum_strings() {
    static const char* const names[kAppletCount] = {
        "Empty",       // kAppletEmpty
        "AD EG",       // kAppletADEG
        "ADSR EG",     // kAppletADSREG
        "AttenOff",    // kAppletAttenuateOffset
        "BinaryCtr",   // kAppletBinary
        "Brancher",    // kAppletBrancher
        "Burst",       // kAppletBurst
        "Button2",     // kAppletButton
        "Calculate",   // kAppletCalculate
        "Calibr8",     // kAppletCalibr8
        "Carpeggio",   // kAppletCarpeggio
        "Chordinate",  // kAppletChordinator
        "Clk2Gate",    // kAppletClkToGate
        "Clk Div",     // kAppletClockDivider
        "Clk Skip",    // kAppletClockSkip
        "Combin8",     // kAppletCombin8
        "Compare",     // kAppletCompare
        "Cumulus",     // kAppletCumulus
        "DualQuant",   // kAppletDualQuant
        "EnigmaJr",    // kAppletEnigmaJr
        "EnsOscKey",   // kAppletEnsOscKey
        "EnvFollow",   // kAppletEnvFollow
        "GameOfLife",  // kAppletGameOfLife
        "GateDelay",   // kAppletGateDelay
        "Gated VCA",   // kAppletGatedVCA
        "Logic",       // kAppletLogic
        "LowerRenz",   // kAppletLowerRenz
        "Metronome",   // kAppletMetronome
        "MultiScale",  // kAppletMultiScale
        "OffsetQuant", // kAppletOffsetQuant
        "Pigeons",     // kAppletPigeons
        "PolyDiv",     // kAppletPolyDiv
        "ProbDiv",     // kAppletProbabilityDivider
        "Relabi",      // kAppletRelabi
        "ResetClock",  // kAppletResetClock
        "RndWalk",     // kAppletRndWalk
        "RunglBook",   // kAppletRunglBook
        "ScaleDuet",   // kAppletScaleDuet
        "SchmittTr",   // kAppletSchmitt
        "Scope",       // kAppletScope
        "ShiftGate",   // kAppletShiftGate
        "Shredder",    // kAppletShredder
        "Shuffle",     // kAppletShuffle
        "Slew",        // kAppletSlew
        "Squanch",     // kAppletSquanch
        "Stairs",      // kAppletStairs
        "Strum",       // kAppletStrum
        "Switch",      // kAppletSwitch
        "TLNeuron",    // kAppletTLNeuron
        "Trending",    // kAppletTrending
        "VectorEG",    // kAppletVectorEG
        "VectorLFO",   // kAppletVectorLFO
        "VectorMod",   // kAppletVectorMod
        "VectorMorph", // kAppletVectorMorph
        "Voltage",     // kAppletVoltage
        "Xfader",      // kAppletXfader
    };
    return names;
}

inline int applet_index_for_name(const char* name) {
    if (!name) return -1;
    const char* const* names = applet_enum_strings();
    for (int i = 0; i < kAppletCount; ++i) {
        const char* a = names[i];
        const char* b = name;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

template <typename T>
constexpr T cmax(T a, T b) { return a > b ? a : b; }

constexpr size_t kMaxAppletSize =
    cmax(sizeof(Empty),
    cmax(sizeof(Logic),
    cmax(sizeof(AttenuateOffset),
    cmax(sizeof(Slew),
    cmax(sizeof(TLNeuron),
    cmax(sizeof(Calculate),
    cmax(sizeof(Brancher),
    cmax(sizeof(GateDelay),
    cmax(sizeof(Button),
    cmax(sizeof(ClkToGate),
    cmax(sizeof(ClockDivider),
    cmax(sizeof(ClockSkip),
    cmax(sizeof(Compare),
    cmax(sizeof(Cumulus),
    cmax(sizeof(EnvFollow),
    cmax(sizeof(GatedVCA),
    cmax(sizeof(PolyDiv),
    cmax(sizeof(RndWalk),
    cmax(sizeof(RunglBook),
    cmax(sizeof(Schmitt),
    cmax(sizeof(Stairs),
    cmax(sizeof(Switch),
    cmax(sizeof(Voltage),
    cmax(sizeof(ADEG),
    cmax(sizeof(ADSREG),
    cmax(sizeof(Binary),
    cmax(sizeof(GameOfLife),
    cmax(sizeof(ProbabilityDivider),
    cmax(sizeof(ShiftGate),
    cmax(sizeof(Trending),
    cmax(sizeof(Burst),
    // Phase 6 additions
    cmax(sizeof(VectorLFO),
    cmax(sizeof(VectorEG),
    cmax(sizeof(VectorMod),
    cmax(sizeof(VectorMorph),
    cmax(sizeof(Relabi),
    cmax(sizeof(LowerRenz),
    cmax(sizeof(Combin8),
    cmax(sizeof(Pigeons),
    cmax(sizeof(Strum),
    cmax(sizeof(Shredder),
    cmax(sizeof(Carpeggio),
    cmax(sizeof(Squanch),
    cmax(sizeof(Chordinator),
    cmax(sizeof(DualQuant),
    cmax(sizeof(EnigmaJr),
    cmax(sizeof(OffsetQuant),
    cmax(sizeof(MultiScale),
    cmax(sizeof(ScaleDuet),
    cmax(sizeof(EnsOscKey),
    cmax(sizeof(Calibr8),
    cmax(sizeof(ResetClock),
    cmax(sizeof(Shuffle),
    cmax(sizeof(Xfader),
    cmax(sizeof(Scope),
         sizeof(Metronome))))))))))))))))))))))))))))))))))))))))))))))))))))))));

constexpr size_t kMaxAppletAlign =
    cmax(alignof(Empty),
    cmax(alignof(Logic),
    cmax(alignof(AttenuateOffset),
    cmax(alignof(Slew),
    cmax(alignof(TLNeuron),
    cmax(alignof(Calculate),
    cmax(alignof(Brancher),
    cmax(alignof(GateDelay),
    cmax(alignof(Button),
    cmax(alignof(ClkToGate),
    cmax(alignof(ClockDivider),
    cmax(alignof(ClockSkip),
    cmax(alignof(Compare),
    cmax(alignof(Cumulus),
    cmax(alignof(EnvFollow),
    cmax(alignof(GatedVCA),
    cmax(alignof(PolyDiv),
    cmax(alignof(RndWalk),
    cmax(alignof(RunglBook),
    cmax(alignof(Schmitt),
    cmax(alignof(Stairs),
    cmax(alignof(Switch),
    cmax(alignof(Voltage),
    cmax(alignof(ADEG),
    cmax(alignof(ADSREG),
    cmax(alignof(Binary),
    cmax(alignof(GameOfLife),
    cmax(alignof(ProbabilityDivider),
    cmax(alignof(ShiftGate),
    cmax(alignof(Trending),
    cmax(alignof(Burst),
    // Phase 6 additions
    cmax(alignof(VectorLFO),
    cmax(alignof(VectorEG),
    cmax(alignof(VectorMod),
    cmax(alignof(VectorMorph),
    cmax(alignof(Relabi),
    cmax(alignof(LowerRenz),
    cmax(alignof(Combin8),
    cmax(alignof(Pigeons),
    cmax(alignof(Strum),
    cmax(alignof(Shredder),
    cmax(alignof(Carpeggio),
    cmax(alignof(Squanch),
    cmax(alignof(Chordinator),
    cmax(alignof(DualQuant),
    cmax(alignof(EnigmaJr),
    cmax(alignof(OffsetQuant),
    cmax(alignof(MultiScale),
    cmax(alignof(ScaleDuet),
    cmax(alignof(EnsOscKey),
    cmax(alignof(Calibr8),
    cmax(alignof(ResetClock),
    cmax(alignof(Shuffle),
    cmax(alignof(Xfader),
    cmax(alignof(Scope),
         alignof(Metronome))))))))))))))))))))))))))))))))))))))))))))))))))))))));

template <class T>
inline HemisphereApplet* make_applet(void* sram) {
    return new (sram) T();
}

using AppletFactory = HemisphereApplet* (*)(void*);

inline AppletFactory applet_factory(AppletIndex idx) {
    static const AppletFactory table[kAppletCount] = {
        &make_applet<Empty>,             // kAppletEmpty
        &make_applet<ADEG>,              // kAppletADEG
        &make_applet<ADSREG>,            // kAppletADSREG
        &make_applet<AttenuateOffset>,   // kAppletAttenuateOffset
        &make_applet<Binary>,            // kAppletBinary
        &make_applet<Brancher>,          // kAppletBrancher
        &make_applet<Burst>,             // kAppletBurst
        &make_applet<Button>,            // kAppletButton
        &make_applet<Calculate>,         // kAppletCalculate
        &make_applet<Calibr8>,           // kAppletCalibr8
        &make_applet<Carpeggio>,         // kAppletCarpeggio
        &make_applet<Chordinator>,       // kAppletChordinator
        &make_applet<ClkToGate>,         // kAppletClkToGate
        &make_applet<ClockDivider>,      // kAppletClockDivider
        &make_applet<ClockSkip>,         // kAppletClockSkip
        &make_applet<Combin8>,           // kAppletCombin8
        &make_applet<Compare>,           // kAppletCompare
        &make_applet<Cumulus>,           // kAppletCumulus
        &make_applet<DualQuant>,         // kAppletDualQuant
        &make_applet<EnigmaJr>,          // kAppletEnigmaJr
        &make_applet<EnsOscKey>,         // kAppletEnsOscKey
        &make_applet<EnvFollow>,         // kAppletEnvFollow
        &make_applet<GameOfLife>,        // kAppletGameOfLife
        &make_applet<GateDelay>,         // kAppletGateDelay
        &make_applet<GatedVCA>,          // kAppletGatedVCA
        &make_applet<Logic>,             // kAppletLogic
        &make_applet<LowerRenz>,         // kAppletLowerRenz
        &make_applet<Metronome>,         // kAppletMetronome
        &make_applet<MultiScale>,        // kAppletMultiScale
        &make_applet<OffsetQuant>,       // kAppletOffsetQuant
        &make_applet<Pigeons>,           // kAppletPigeons
        &make_applet<PolyDiv>,           // kAppletPolyDiv
        &make_applet<ProbabilityDivider>,// kAppletProbabilityDivider
        &make_applet<Relabi>,            // kAppletRelabi
        &make_applet<ResetClock>,        // kAppletResetClock
        &make_applet<RndWalk>,           // kAppletRndWalk
        &make_applet<RunglBook>,         // kAppletRunglBook
        &make_applet<ScaleDuet>,         // kAppletScaleDuet
        &make_applet<Schmitt>,           // kAppletSchmitt
        &make_applet<Scope>,             // kAppletScope
        &make_applet<ShiftGate>,         // kAppletShiftGate
        &make_applet<Shredder>,          // kAppletShredder
        &make_applet<Shuffle>,           // kAppletShuffle
        &make_applet<Slew>,              // kAppletSlew
        &make_applet<Squanch>,           // kAppletSquanch
        &make_applet<Stairs>,            // kAppletStairs
        &make_applet<Strum>,             // kAppletStrum
        &make_applet<Switch>,            // kAppletSwitch
        &make_applet<TLNeuron>,          // kAppletTLNeuron
        &make_applet<Trending>,          // kAppletTrending
        &make_applet<VectorEG>,          // kAppletVectorEG
        &make_applet<VectorLFO>,         // kAppletVectorLFO
        &make_applet<VectorMod>,         // kAppletVectorMod
        &make_applet<VectorMorph>,       // kAppletVectorMorph
        &make_applet<Voltage>,           // kAppletVoltage
        &make_applet<Xfader>,            // kAppletXfader
    };
    return table[idx];
}

}  // namespace hem_shim
