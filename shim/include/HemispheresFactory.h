#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
#include "applet_indices.h"
#include "HemisphereApplet.h"
#include "PackingUtils.h"

// Pre-include vendor-dep prereqs. These define DMAMEM (no-op on host and
// NT), InterpLinear16 (used by vendor HSVectorOscillator.h), and ensure
// shim's util_math.h is in scope before vendor headers pull their own
// util_math.h via relative include. Shim util_math.h pre-defines the
// vendor traditional include guard UTIL_MATH_H_ so vendor's body
// becomes a no-op once shim's is in scope.
#include "vector_osc/vec_osc_prereqs.h"
// Pre-include shim copies of vendor-dep headers that carry traditional
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
#include "ADEG.h"
#include "ADSREG.h"
#include "Binary.h"
#include "GameOfLife.h"
#include "ProbabilityDivider.h"
#include "ShiftGate.h"
#include "Trending.h"
// Plug-in variant selector. Set via Makefile per .o:
//   HEMI_VARIANT=0  host build, full 56-applet factory (default; tests need it)
//   HEMI_VARIANT=1  ARM Hemispheres.o primary: 51 applets, dropping the 5
//                   largest (Relabi, Shredder, EnsOscKey, VectorLFO, Strum)
//                   so the build fits under the ~82KB per-plugin .text
//                   budget the NT firmware enforces. Measured: 81566 bytes
//                   .text on hardware.
//   HEMI_VARIANT=2  ARM Hemispheres2.o secondary: registers ONLY the 5
//                   dropped applets so they remain reachable on hardware.
//
// Each ARM variant must fit under the .text cap independently; the cap is
// per-.o, not per-host. View Info on the NT screen reports "Not enough
// memory for .text : <name>" if a plug-in exceeds it.
#ifndef HEMI_VARIANT
#define HEMI_VARIANT 0
#endif
#define HEMI_IN_PRIMARY   (HEMI_VARIANT == 0 || HEMI_VARIANT == 1)
#define HEMI_IN_SECONDARY (HEMI_VARIANT == 0 || HEMI_VARIANT == 2)

// Applets backed by VectorOscillator / Lorenz / Relabi managers.
#if HEMI_IN_SECONDARY
#include "VectorLFO.h"
#endif
#if HEMI_IN_PRIMARY
#include "VectorEG.h"
#include "VectorMod.h"
#include "VectorMorph.h"
#endif
#if HEMI_IN_SECONDARY
#include "Relabi.h"
#endif
#if HEMI_IN_PRIMARY
#include "LowerRenz.h"
#include "Combin8.h"
#endif
// Applets using the braids quantizer.
#if HEMI_IN_PRIMARY
#include "Pigeons.h"
#endif
#if HEMI_IN_SECONDARY
#include "Strum.h"
#include "Shredder.h"
#endif
#if HEMI_IN_PRIMARY
#include "Carpeggio.h"
#include "Squanch.h"
#include "Chordinator.h"
#include "DualQuant.h"
#include "EnigmaJr.h"
#include "OffsetQuant.h"
#include "MultiScale.h"
#include "ScaleDuet.h"
#endif
#if HEMI_IN_SECONDARY
#include "EnsOscKey.h"
#endif
#if HEMI_IN_PRIMARY
#include "Calibr8.h"
// Applets using shared host helpers (step_n_inner_ticks, etc.).
#include "ResetClock.h"
#include "Shuffle.h"
#include "Xfader.h"
#include "Scope.h"
// Applets driven by HS::clock_m.
#include "Metronome.h"
#endif

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

// kMaxAppletSize / kMaxAppletAlign size the per-side applet buffers in
// HemispheresInstance. ARM variants don't see all applet types so a sizeof
// chain over the full set would fail to compile. Hardcode generous values
// that fit any realistic Hemisphere applet (largest measured: ~700 bytes).
// HemispheresInstance uses 2 * kMaxAppletSize for sram_left + sram_right.
#if HEMI_VARIANT == 0
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
#else
constexpr size_t kMaxAppletSize = 1024;
#endif

#if HEMI_VARIANT == 0
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
#else
constexpr size_t kMaxAppletAlign = 16;
#endif

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
#if HEMI_IN_PRIMARY
        &make_applet<Calibr8>,           // kAppletCalibr8
        &make_applet<Carpeggio>,         // kAppletCarpeggio
        &make_applet<Chordinator>,       // kAppletChordinator
#else
        &make_applet<Empty>,             // kAppletCalibr8 (primary-only)
        &make_applet<Empty>,             // kAppletCarpeggio (primary-only)
        &make_applet<Empty>,             // kAppletChordinator (primary-only)
#endif
        &make_applet<ClkToGate>,         // kAppletClkToGate
        &make_applet<ClockDivider>,      // kAppletClockDivider
        &make_applet<ClockSkip>,         // kAppletClockSkip
#if HEMI_IN_PRIMARY
        &make_applet<Combin8>,           // kAppletCombin8
#else
        &make_applet<Empty>,             // kAppletCombin8 (primary-only)
#endif
        &make_applet<Compare>,           // kAppletCompare
        &make_applet<Cumulus>,           // kAppletCumulus
#if HEMI_IN_PRIMARY
        &make_applet<DualQuant>,         // kAppletDualQuant
        &make_applet<EnigmaJr>,          // kAppletEnigmaJr
#else
        &make_applet<Empty>,             // kAppletDualQuant (primary-only)
        &make_applet<Empty>,             // kAppletEnigmaJr (primary-only)
#endif
#if HEMI_IN_SECONDARY
        &make_applet<EnsOscKey>,         // kAppletEnsOscKey
#else
        &make_applet<Empty>,             // kAppletEnsOscKey (secondary-only)
#endif
        &make_applet<EnvFollow>,         // kAppletEnvFollow
        &make_applet<GameOfLife>,        // kAppletGameOfLife
        &make_applet<GateDelay>,         // kAppletGateDelay
        &make_applet<GatedVCA>,          // kAppletGatedVCA
        &make_applet<Logic>,             // kAppletLogic
#if HEMI_IN_PRIMARY
        &make_applet<LowerRenz>,         // kAppletLowerRenz
        &make_applet<Metronome>,         // kAppletMetronome
        &make_applet<MultiScale>,        // kAppletMultiScale
        &make_applet<OffsetQuant>,       // kAppletOffsetQuant
        &make_applet<Pigeons>,           // kAppletPigeons
#else
        &make_applet<Empty>,             // kAppletLowerRenz (primary-only)
        &make_applet<Empty>,             // kAppletMetronome (primary-only)
        &make_applet<Empty>,             // kAppletMultiScale (primary-only)
        &make_applet<Empty>,             // kAppletOffsetQuant (primary-only)
        &make_applet<Empty>,             // kAppletPigeons (primary-only)
#endif
        &make_applet<PolyDiv>,           // kAppletPolyDiv
        &make_applet<ProbabilityDivider>,// kAppletProbabilityDivider
#if HEMI_IN_SECONDARY
        &make_applet<Relabi>,            // kAppletRelabi
#else
        &make_applet<Empty>,             // kAppletRelabi (secondary-only)
#endif
#if HEMI_IN_PRIMARY
        &make_applet<ResetClock>,        // kAppletResetClock
#else
        &make_applet<Empty>,             // kAppletResetClock (primary-only)
#endif
        &make_applet<RndWalk>,           // kAppletRndWalk
        &make_applet<RunglBook>,         // kAppletRunglBook
#if HEMI_IN_PRIMARY
        &make_applet<ScaleDuet>,         // kAppletScaleDuet
#else
        &make_applet<Empty>,             // kAppletScaleDuet (primary-only)
#endif
        &make_applet<Schmitt>,           // kAppletSchmitt
#if HEMI_IN_PRIMARY
        &make_applet<Scope>,             // kAppletScope
#else
        &make_applet<Empty>,             // kAppletScope (primary-only)
#endif
        &make_applet<ShiftGate>,         // kAppletShiftGate
#if HEMI_IN_SECONDARY
        &make_applet<Shredder>,          // kAppletShredder
#else
        &make_applet<Empty>,             // kAppletShredder (secondary-only)
#endif
#if HEMI_IN_PRIMARY
        &make_applet<Shuffle>,           // kAppletShuffle
#else
        &make_applet<Empty>,             // kAppletShuffle (primary-only)
#endif
        &make_applet<Slew>,              // kAppletSlew
#if HEMI_IN_PRIMARY
        &make_applet<Squanch>,           // kAppletSquanch
#else
        &make_applet<Empty>,             // kAppletSquanch (primary-only)
#endif
        &make_applet<Stairs>,            // kAppletStairs
#if HEMI_IN_SECONDARY
        &make_applet<Strum>,             // kAppletStrum
#else
        &make_applet<Empty>,             // kAppletStrum (secondary-only)
#endif
        &make_applet<Switch>,            // kAppletSwitch
        &make_applet<TLNeuron>,          // kAppletTLNeuron
        &make_applet<Trending>,          // kAppletTrending
#if HEMI_IN_PRIMARY
        &make_applet<VectorEG>,          // kAppletVectorEG
#else
        &make_applet<Empty>,             // kAppletVectorEG (primary-only)
#endif
#if HEMI_IN_SECONDARY
        &make_applet<VectorLFO>,         // kAppletVectorLFO
#else
        &make_applet<Empty>,             // kAppletVectorLFO (secondary-only)
#endif
#if HEMI_IN_PRIMARY
        &make_applet<VectorMod>,         // kAppletVectorMod
        &make_applet<VectorMorph>,       // kAppletVectorMorph
#else
        &make_applet<Empty>,             // kAppletVectorMod (primary-only)
        &make_applet<Empty>,             // kAppletVectorMorph (primary-only)
#endif
        &make_applet<Voltage>,           // kAppletVoltage
#if HEMI_IN_PRIMARY
        &make_applet<Xfader>,            // kAppletXfader
#else
        &make_applet<Empty>,             // kAppletXfader (primary-only)
#endif
    };
    return table[idx];
}

}  // namespace hem_shim
