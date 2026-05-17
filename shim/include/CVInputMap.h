#pragma once
#include "HSIOFrame.h"

class CVInputMap {
public:
    int channel = 0;
    int In() const { return HS::frame.inputs[channel]; }
};

extern CVInputMap cvmap[4];

class DigitalInputMap {
public:
    int channel = 0;
    bool Gate() const { return HS::frame.clocked[channel]; }
};

extern DigitalInputMap trigmap[4];
