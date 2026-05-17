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
    kAppletEmpty = 0,
    kAppletLogic,
    kAppletAttenuateOffset,
    kAppletSlew,
    kAppletCalculate,
    kAppletBurst,
    kAppletCount
};

inline const char* const* applet_enum_strings() {
    static const char* const names[kAppletCount] = {
        "Empty", "Logic", "AttenOff", "Slew", "Calculate", "Burst"
    };
    return names;
}

constexpr size_t kMaxAppletSize = std::max({
    sizeof(Empty),
    sizeof(Logic),
    sizeof(AttenuateOffset),
    sizeof(Slew),
    sizeof(Calculate),
    sizeof(Burst)
});

constexpr size_t kMaxAppletAlign = std::max({
    alignof(Empty),
    alignof(Logic),
    alignof(AttenuateOffset),
    alignof(Slew),
    alignof(Calculate),
    alignof(Burst)
});

template <class T>
inline HemisphereApplet* make_applet(void* sram) {
    return new (sram) T();
}

using AppletFactory = HemisphereApplet* (*)(void*);

inline AppletFactory applet_factory(AppletIndex idx) {
    static const AppletFactory table[kAppletCount] = {
        &make_applet<Empty>,
        &make_applet<Logic>,
        &make_applet<AttenuateOffset>,
        &make_applet<Slew>,
        &make_applet<Calculate>,
        &make_applet<Burst>,
    };
    return table[idx];
}

}  // namespace hem_shim
