#pragma once
#include "HemisphereApplet.h"

class Empty : public HemisphereApplet {
public:
    const char* applet_name() override { return "Empty"; }

    void Start() override {}
    void Controller() override {}
    void View() override {
        gfxPrint(34, 28, "Pick applet");
    }
    uint64_t OnDataRequest() override { return 0; }
    void OnDataReceive(uint64_t) override {}
    void OnEncoderMove(int) override {}

protected:
    void SetHelp() override {}
};
