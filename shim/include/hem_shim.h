#pragma once

#ifndef NT_HEM_NO_IMPL
#include "hem_shim_impl.h"
#define NT_HEM_NO_IMPL 1
#endif

#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <new>
#include <cstring>
#include "HemisphereApplet.h"
#include "HSIOFrame.h"

namespace hem_shim {

enum {
    kParamGateIn1, kParamGateIn2,
    kParamCvIn1,   kParamCvIn2,
    kParamCvOut1,  kParamCvOut1Mode,
    kParamCvOut2,  kParamCvOut2Mode,
    kParamCount
};

inline const _NT_parameter* shim_parameters() {
    static const _NT_parameter params[] = {
        NT_PARAMETER_CV_INPUT("Gate (ch A)", 0, 1)
        NT_PARAMETER_CV_INPUT("Gate (ch B)", 0, 2)
        NT_PARAMETER_CV_INPUT("CV (ch A)",   0, 5)
        NT_PARAMETER_CV_INPUT("CV (ch B)",   0, 6)
        NT_PARAMETER_IO("Out (ch A)", 0, 13, kNT_unitCvOutput)
        { .name = "Out (ch A) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
        NT_PARAMETER_IO("Out (ch B)", 0, 14, kNT_unitCvOutput)
        { .name = "Out (ch B) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
    };
    return params;
}

inline const _NT_parameterPages* shim_parameter_pages() {
    static const uint8_t routing_page[] = {
        kParamGateIn1, kParamGateIn2, kParamCvIn1, kParamCvIn2,
        kParamCvOut1, kParamCvOut1Mode, kParamCvOut2, kParamCvOut2Mode
    };
    static const _NT_parameterPage pages[] = {
        { .name = "Routing", .numParams = sizeof(routing_page), .params = routing_page },
    };
    static const _NT_parameterPages parameterPages = {
        .numPages = 1, .pages = pages,
    };
    return &parameterPages;
}

template <typename T>
struct AlgorithmInstance : public _NT_algorithm {
    T applet;
    bool started = false;
};

template <typename T>
struct Shim {
    static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
        req.numParameters = kParamCount;
        req.sram = sizeof(AlgorithmInstance<T>);
    }

    static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                    const _NT_algorithmRequirements&, const int32_t*) {
        std::memset(ptrs.sram, 0, sizeof(AlgorithmInstance<T>));
        auto* alg = new (ptrs.sram) AlgorithmInstance<T>();
        alg->parameters     = shim_parameters();
        alg->parameterPages = shim_parameter_pages();
        alg->applet.BaseStart(HS::LEFT_HEMISPHERE);
        alg->started = true;
        return alg;
    }

    static void copy_bus_to_frame(int bus_param, int* dst, float* busFrames, int numFrames,
                                  const int16_t* v) {
        int bus = v[bus_param];
        if (bus <= 0) { *dst = 0; return; }
        const float* src = busFrames + (bus - 1) * numFrames;
        float sum = 0.0f;
        for (int i = 0; i < numFrames; ++i) sum += src[i];
        float mean = sum / (float)numFrames;
        *dst = (int)(mean * 1536.0f);
    }

    struct GateRead { bool rising; bool high; };
    static GateRead read_gate(int bus_param, float* busFrames, int numFrames, const int16_t* v,
                              bool& prev_high) {
        int bus = v[bus_param];
        if (bus <= 0) { prev_high = false; return { false, false }; }
        const float* src = busFrames + (bus - 1) * numFrames;
        bool rising = false;
        bool last_high = prev_high;
        for (int i = 0; i < numFrames; ++i) {
            bool high = (src[i] > 0.5f);
            if (high && !last_high) rising = true;
            last_high = high;
        }
        prev_high = last_high;
        return { rising, last_high };
    }

    static void write_frame_to_bus(int bus_param, int mode_param, int value_hem,
                                   float* busFrames, int numFrames, const int16_t* v) {
        int bus = v[bus_param];
        if (bus <= 0) return;
        float* dst = busFrames + (bus - 1) * numFrames;
        float value_nt = (float)value_hem / 1536.0f;
        bool replace = v[mode_param];
        if (replace) {
            for (int i = 0; i < numFrames; ++i) dst[i] = value_nt;
        } else {
            for (int i = 0; i < numFrames; ++i) dst[i] += value_nt;
        }
    }

    static bool& prev_gate(int idx) {
        static bool prev[2] = { false, false };
        return prev[idx];
    }

    static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        int numFrames = numFramesBy4 * 4;
        const int16_t* v = alg->v;

        copy_bus_to_frame(kParamCvIn1, &HS::frame.inputs[0], busFrames, numFrames, v);
        copy_bus_to_frame(kParamCvIn2, &HS::frame.inputs[1], busFrames, numFrames, v);
        { auto g = read_gate(kParamGateIn1, busFrames, numFrames, v, prev_gate(0));
          HS::frame.clocked[0] = g.rising; HS::frame.gate_high[0] = g.high; }
        { auto g = read_gate(kParamGateIn2, busFrames, numFrames, v, prev_gate(1));
          HS::frame.clocked[1] = g.rising; HS::frame.gate_high[1] = g.high; }

        // Hemisphere assumes ~16.66kHz Controller calls (60us tick).
        // NT step ~ numFrames/48kHz; one Controller call per ~3 audio samples
        // keeps Hemisphere's tick semantics roughly correct.
        int ticks_this_step = numFrames / 3;
        if (ticks_this_step < 1) ticks_this_step = 1;
        for (int i = 0; i < ticks_this_step; ++i) {
            OC::CORE::ticks += 1;
            for (int ch = 0; ch < 4; ++ch) {
                if (HS::frame.clock_countdown[ch] > 0) {
                    if (--HS::frame.clock_countdown[ch] == 0)
                        HS::frame.outputs[ch].set(0);
                }
            }
            alg->applet.Controller();
        }

        write_frame_to_bus(kParamCvOut1, kParamCvOut1Mode, HS::frame.outputs[0].value,
                          busFrames, numFrames, v);
        write_frame_to_bus(kParamCvOut2, kParamCvOut2Mode, HS::frame.outputs[1].value,
                          busFrames, numFrames, v);
    }

    static bool draw(_NT_algorithm* self) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        if (!alg->started) return false;
        alg->applet.View();
        return false;
    }

    static uint32_t hasCustomUi(_NT_algorithm*) {
        return kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR;
    }

    static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        if (data.encoders[0] != 0) alg->applet.OnEncoderMove(data.encoders[0]);
        if (data.encoders[1] != 0) {
            bool was = HS::enc_edit[HS::LEFT_HEMISPHERE].isEditing;
            HS::enc_edit[HS::LEFT_HEMISPHERE].isEditing = true;
            alg->applet.OnEncoderMove(data.encoders[1]);
            HS::enc_edit[HS::LEFT_HEMISPHERE].isEditing = was;
        }
        if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
            alg->applet.OnButtonPress();
        }
        if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR)) {
            alg->applet.AuxButton();
        }
    }

    static void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        uint64_t state = alg->applet.OnDataRequest();
        uint32_t hi = (uint32_t)(state >> 32);
        uint32_t lo = (uint32_t)(state & 0xFFFFFFFFu);
        stream.addMemberName("hem_hi"); stream.addNumber((int)hi);
        stream.addMemberName("hem_lo"); stream.addNumber((int)lo);
    }

    static bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
        auto* alg = static_cast<AlgorithmInstance<T>*>(self);
        int num_members = 0;
        if (!parse.numberOfObjectMembers(num_members)) return false;
        int hi = 0, lo = 0;
        bool found_hi = false, found_lo = false;
        for (int i = 0; i < num_members; ++i) {
            if      (parse.matchName("hem_hi")) { if (!parse.number(hi)) return false; found_hi = true; }
            else if (parse.matchName("hem_lo")) { if (!parse.number(lo)) return false; found_lo = true; }
            else                                { if (!parse.skipMember()) return false; }
        }
        if (!(found_hi && found_lo)) return true;
        uint64_t state = ((uint64_t)(uint32_t)hi << 32) | (uint64_t)(uint32_t)lo;
        alg->applet.OnDataReceive(state);
        return true;
    }
};

}  // namespace hem_shim

#define NT_HEM_PLUGIN(klass, guid_str_4chars, name_str, desc_str) \
    static const _NT_factory _hem_factory = { \
        .guid = NT_MULTICHAR(guid_str_4chars[0], guid_str_4chars[1], \
                             guid_str_4chars[2], guid_str_4chars[3]), \
        .name = name_str, \
        .description = desc_str, \
        .numSpecifications = 0, \
        .calculateRequirements = hem_shim::Shim<klass>::calculateRequirements, \
        .construct             = hem_shim::Shim<klass>::construct, \
        .step                  = hem_shim::Shim<klass>::step, \
        .draw                  = hem_shim::Shim<klass>::draw, \
        .tags                  = kNT_tagUtility, \
        .hasCustomUi           = hem_shim::Shim<klass>::hasCustomUi, \
        .customUi              = hem_shim::Shim<klass>::customUi, \
        .serialise             = hem_shim::Shim<klass>::serialise, \
        .deserialise           = hem_shim::Shim<klass>::deserialise, \
    }; \
    extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) { \
        switch (selector) { \
        case kNT_selector_version:      return kNT_apiVersionCurrent; \
        case kNT_selector_numFactories: return 1; \
        case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &_hem_factory : nullptr); \
        } \
        return 0; \
    }
