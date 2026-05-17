#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
#include "HemisphereApplet.h"
#include "Empty.h"
#include "Logic.h"
#include "AttenuateOffset.h"
#include "Brancher.h"
#include "Slew.h"
#include "TLNeuron.h"
#include "Calculate.h"
#include "Burst.h"

namespace hem_shim {

enum AppletIndex : uint8_t {
    kAppletEmpty = 0,           // sentinel: always index 0 (default selector)
    kAppletAttenuateOffset,
    kAppletBrancher,
    kAppletBurst,
    kAppletCalculate,
    kAppletLogic,
    kAppletSlew,
    kAppletTLNeuron,
    kAppletCount
};

inline const char* const* applet_enum_strings() {
    static const char* const names[kAppletCount] = {
        "Empty", "AttenOff", "Brancher", "Burst", "Calculate", "Logic", "Slew", "TLNeuron"
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
         sizeof(Burst))))))));

constexpr size_t kMaxAppletAlign =
    cmax(alignof(Empty),
    cmax(alignof(Logic),
    cmax(alignof(AttenuateOffset),
    cmax(alignof(Slew),
    cmax(alignof(TLNeuron),
    cmax(alignof(Calculate),
    cmax(alignof(Brancher),
         alignof(Burst))))))));

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
        &make_applet<Calculate>,
        &make_applet<Logic>,
        &make_applet<Slew>,
        &make_applet<TLNeuron>,
    };
    return table[idx];
}

}  // namespace hem_shim
