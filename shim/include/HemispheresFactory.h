#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
#include "applet_indices.h"
#include "HemisphereApplet.h"
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

namespace hem_shim {

inline const char* const* applet_enum_strings() {
    static const char* const names[kAppletCount] = {
        "Empty", "AD EG", "ADSR EG", "AttenOff", "BinaryCtr", "Brancher", "Burst",
        "Button2", "Calculate", "Clk2Gate", "Clk Div", "Clk Skip", "Compare", "Cumulus",
        "EnvFollow", "GameOfLife", "GateDelay", "Gated VCA", "Logic", "PolyDiv",
        "ProbDiv", "RndWalk", "RunglBook", "SchmittTr", "ShiftGate", "Slew",
        "Stairs", "Switch", "TLNeuron", "Trending", "Voltage"
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
         sizeof(Burst)))))))))))))))))))))))))))))));

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
         alignof(Burst)))))))))))))))))))))))))))))));

template <class T>
inline HemisphereApplet* make_applet(void* sram) {
    return new (sram) T();
}

using AppletFactory = HemisphereApplet* (*)(void*);

inline AppletFactory applet_factory(AppletIndex idx) {
    static const AppletFactory table[kAppletCount] = {
        &make_applet<Empty>,
        &make_applet<ADEG>,
        &make_applet<ADSREG>,
        &make_applet<AttenuateOffset>,
        &make_applet<Binary>,
        &make_applet<Brancher>,
        &make_applet<Burst>,
        &make_applet<Button>,
        &make_applet<Calculate>,
        &make_applet<ClkToGate>,
        &make_applet<ClockDivider>,
        &make_applet<ClockSkip>,
        &make_applet<Compare>,
        &make_applet<Cumulus>,
        &make_applet<EnvFollow>,
        &make_applet<GameOfLife>,
        &make_applet<GateDelay>,
        &make_applet<GatedVCA>,
        &make_applet<Logic>,
        &make_applet<PolyDiv>,
        &make_applet<ProbabilityDivider>,
        &make_applet<RndWalk>,
        &make_applet<RunglBook>,
        &make_applet<Schmitt>,
        &make_applet<ShiftGate>,
        &make_applet<Slew>,
        &make_applet<Stairs>,
        &make_applet<Switch>,
        &make_applet<TLNeuron>,
        &make_applet<Trending>,
        &make_applet<Voltage>,
    };
    return table[idx];
}

}  // namespace hem_shim
