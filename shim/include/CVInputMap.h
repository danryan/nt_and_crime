#pragma once
#include "HSIOFrame.h"

class CVInputMap {
public:
    int channel;
    explicit CVInputMap(int ch = 0) : channel(ch) {}
    int In() const { return HS::frame.inputs[channel]; }
};

extern CVInputMap cvmap[4];

class DigitalInputMap {
public:
    int channel;
    explicit DigitalInputMap(int ch = 0) : channel(ch) {}
    bool Gate() const { return HS::frame.clocked[channel]; }
};

extern DigitalInputMap trigmap[4];
