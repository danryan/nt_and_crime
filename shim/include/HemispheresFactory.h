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
#include "Compare.h"
#include "Cumulus.h"
#include "GateDelay.h"
#include "GatedVCA.h"

namespace hem_shim {

inline const char* const* applet_enum_strings() {
    static const char* const names[kAppletCount] = {
        "Empty", "AttenOff", "Brancher", "Burst", "Button2", "Calculate", "Clk2Gate", "Compare", "Cumulus", "GateDelay", "Gated VCA", "Logic", "Slew", "TLNeuron"
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
    cmax(sizeof(Compare),
    cmax(sizeof(Cumulus),
    cmax(sizeof(GatedVCA),
         sizeof(Burst))))))))))))));

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
    cmax(alignof(Compare),
    cmax(alignof(Cumulus),
    cmax(alignof(GatedVCA),
         alignof(Burst))))))))))))));

template <class T>
inline HemisphereApplet* make_applet(void* sram) {
    return new (sram) T();
}

using AppletFactory = HemisphereApplet* (*)(void*);

inline AppletFactory applet_factory(AppletIndex idx) {
    static const AppletFactory table[kAppletCount] = {
        &make_applet<Empty>,
        &make_applet<AttenuateOffset>,
        &make_applet<Brancher>,
        &make_applet<Burst>,
        &make_applet<Button>,
        &make_applet<Calculate>,
        &make_applet<ClkToGate>,
        &make_applet<Compare>,
        &make_applet<Cumulus>,
        &make_applet<GateDelay>,
        &make_applet<GatedVCA>,
        &make_applet<Logic>,
        &make_applet<Slew>,
        &make_applet<TLNeuron>,
    };
    return table[idx];
}

}  // namespace hem_shim
