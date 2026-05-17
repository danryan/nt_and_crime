#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
#include "HemisphereApplet.h"
#include "Empty.h"
#include "Logic.h"
#include "AttenuateOffset.h"
#include "Slew.h"
#include "Calculate.h"
#include "Burst.h"

namespace hem_shim {

enum AppletIndex : uint8_t {
    kAppletEmpty = 0,           // sentinel: always index 0 (default selector)
    kAppletAttenuateOffset,
    kAppletBurst,
    kAppletCalculate,
    kAppletLogic,
    kAppletSlew,
    kAppletCount
};

inline const char* const* applet_enum_strings() {
    static const char* const names[kAppletCount] = {
        "Empty", "AttenOff", "Burst", "Calculate", "Logic", "Slew"
    };
    return names;
}

template <typename T>
constexpr T cmax(T a, T b) { return a > b ? a : b; }

constexpr size_t kMaxAppletSize =
    cmax(sizeof(Empty),
    cmax(sizeof(Logic),
    cmax(sizeof(AttenuateOffset),
    cmax(sizeof(Slew),
    cmax(sizeof(Calculate),
         sizeof(Burst))))));

constexpr size_t kMaxAppletAlign =
    cmax(alignof(Empty),
    cmax(alignof(Logic),
    cmax(alignof(AttenuateOffset),
    cmax(alignof(Slew),
    cmax(alignof(Calculate),
         alignof(Burst))))));

template <class T>
inline HemisphereApplet* make_applet(void* sram) {
    return new (sram) T();
}

using AppletFactory = HemisphereApplet* (*)(void*);

inline AppletFactory applet_factory(AppletIndex idx) {
    static const AppletFactory table[kAppletCount] = {
        &make_applet<Empty>,
        &make_applet<AttenuateOffset>,
        &make_applet<Burst>,
        &make_applet<Calculate>,
        &make_applet<Logic>,
        &make_applet<Slew>,
    };
    return table[idx];
}

}  // namespace hem_shim
