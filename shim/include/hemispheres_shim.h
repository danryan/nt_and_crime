#pragma once

#include "hem_shim.h"
#include "HemispheresFactory.h"

namespace hem_shim {

// ---------------------------------------------------------------------------
// Hemispheres plug-in: pair-only with runtime applet selectors per side.
// ---------------------------------------------------------------------------

enum {
    kHemSelLeft, kHemSelRight,
    kHemGateInA, kHemGateInB, kHemGateInC, kHemGateInD,
    kHemCvInA,   kHemCvInB,   kHemCvInC,   kHemCvInD,
    kHemCvOutA,  kHemCvOutAMode,
    kHemCvOutB,  kHemCvOutBMode,
    kHemCvOutC,  kHemCvOutCMode,
    kHemCvOutD,  kHemCvOutDMode,
    kHemParamCount
};

inline const _NT_parameter* hemispheres_parameters() {
    static const _NT_parameter params[] = {
        { .name = "Left applet",  .min = 0, .max = kAppletCount - 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = applet_enum_strings() },
        { .name = "Right applet", .min = 0, .max = kAppletCount - 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = applet_enum_strings() },
        NT_PARAMETER_CV_INPUT("Gate (ch A)", 0, 1)
        NT_PARAMETER_CV_INPUT("Gate (ch B)", 0, 2)
        NT_PARAMETER_CV_INPUT("Gate (ch C)", 0, 3)
        NT_PARAMETER_CV_INPUT("Gate (ch D)", 0, 4)
        NT_PARAMETER_CV_INPUT("CV (ch A)",   0, 5)
        NT_PARAMETER_CV_INPUT("CV (ch B)",   0, 6)
        NT_PARAMETER_CV_INPUT("CV (ch C)",   0, 7)
        NT_PARAMETER_CV_INPUT("CV (ch D)",   0, 8)
        NT_PARAMETER_IO("Out (ch A)", 0, 13, kNT_unitCvOutput)
        { .name = "Out (ch A) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
        NT_PARAMETER_IO("Out (ch B)", 0, 14, kNT_unitCvOutput)
        { .name = "Out (ch B) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
        NT_PARAMETER_IO("Out (ch C)", 0, 15, kNT_unitCvOutput)
        { .name = "Out (ch C) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
        NT_PARAMETER_IO("Out (ch D)", 0, 16, kNT_unitCvOutput)
        { .name = "Out (ch D) mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },
    };
    return params;
}

inline const _NT_parameterPages* hemispheres_parameter_pages() {
    static const uint8_t setup_page[] = { kHemSelLeft, kHemSelRight };
    static const uint8_t routing_page[] = {
        kHemGateInA, kHemGateInB, kHemGateInC, kHemGateInD,
        kHemCvInA,   kHemCvInB,   kHemCvInC,   kHemCvInD,
        kHemCvOutA,  kHemCvOutAMode,
        kHemCvOutB,  kHemCvOutBMode,
        kHemCvOutC,  kHemCvOutCMode,
        kHemCvOutD,  kHemCvOutDMode,
    };
    static const _NT_parameterPage pages[] = {
        { .name = "Setup",   .numParams = sizeof(setup_page),   .params = setup_page },
        { .name = "Routing", .numParams = sizeof(routing_page), .params = routing_page },
    };
    static const _NT_parameterPages parameterPages = {
        .numPages = 2, .pages = pages,
    };
    return &parameterPages;
}

struct HemispheresInstance : public _NT_algorithm {
    alignas(kMaxAppletAlign) uint8_t sram_left[kMaxAppletSize];
    alignas(kMaxAppletAlign) uint8_t sram_right[kMaxAppletSize];
    HemisphereApplet* left  = nullptr;
    HemisphereApplet* right = nullptr;
    uint8_t cached_idx_left  = 0;
    uint8_t cached_idx_right = 0;
    bool started = false;
};

inline void hemispheres_reset_side(int side, int offset) {
    HS::frame.outputs[offset + 0].set(0);
    HS::frame.outputs[offset + 1].set(0);
    HS::frame.clock_countdown[offset + 0] = 0;
    HS::frame.clock_countdown[offset + 1] = 0;
    HS::cursor_countdown[side] = 0;
    HS::enc_edit[side].isEditing = false;
}

inline void hemispheres_swap(HemisphereApplet*& slot, void* sram, uint8_t new_idx,
                             HS::HEM_SIDE side) {
    if (slot) slot->~HemisphereApplet();
    int offset = side * 2;
    hemispheres_reset_side(side, offset);
    slot = applet_factory(static_cast<AppletIndex>(new_idx))(sram);
    slot->BaseStart(side);
}

struct HemispheresShim {
    static bool& prev_gate(int idx) {
        static bool prev[4] = { false, false, false, false };
        return prev[idx];
    }

    static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
        req.numParameters = kHemParamCount;
        req.sram = sizeof(HemispheresInstance);
    }

    static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                    const _NT_algorithmRequirements&, const int32_t*) {
        std::memset(ptrs.sram, 0, sizeof(HemispheresInstance));
        auto* alg = new (ptrs.sram) HemispheresInstance();
        alg->parameters     = hemispheres_parameters();
        alg->parameterPages = hemispheres_parameter_pages();
        alg->cached_idx_left  = 0;
        alg->cached_idx_right = 0;
        alg->left  = applet_factory(kAppletEmpty)(alg->sram_left);
        alg->right = applet_factory(kAppletEmpty)(alg->sram_right);
        alg->left->BaseStart(HS::LEFT_HEMISPHERE);
        alg->right->BaseStart(HS::RIGHT_HEMISPHERE);
        alg->started = true;
        return alg;
    }

    static void maybe_swap(HemispheresInstance* alg) {
        uint8_t want_l = (uint8_t)alg->v[kHemSelLeft];
        uint8_t want_r = (uint8_t)alg->v[kHemSelRight];
        if (want_l != alg->cached_idx_left) {
            hemispheres_swap(alg->left, alg->sram_left, want_l, HS::LEFT_HEMISPHERE);
            alg->cached_idx_left = want_l;
        }
        if (want_r != alg->cached_idx_right) {
            hemispheres_swap(alg->right, alg->sram_right, want_r, HS::RIGHT_HEMISPHERE);
            alg->cached_idx_right = want_r;
        }
    }

    static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        maybe_swap(alg);

        int numFrames = numFramesBy4 * 4;
        const int16_t* v = alg->v;

        copy_bus_to_frame(kHemCvInA, &HS::frame.inputs[0], busFrames, numFrames, v);
        copy_bus_to_frame(kHemCvInB, &HS::frame.inputs[1], busFrames, numFrames, v);
        copy_bus_to_frame(kHemCvInC, &HS::frame.inputs[2], busFrames, numFrames, v);
        copy_bus_to_frame(kHemCvInD, &HS::frame.inputs[3], busFrames, numFrames, v);

        { auto g = read_gate(kHemGateInA, busFrames, numFrames, v, prev_gate(0));
          HS::frame.clocked[0] = g.rising; HS::frame.gate_high[0] = g.high; }
        { auto g = read_gate(kHemGateInB, busFrames, numFrames, v, prev_gate(1));
          HS::frame.clocked[1] = g.rising; HS::frame.gate_high[1] = g.high; }
        { auto g = read_gate(kHemGateInC, busFrames, numFrames, v, prev_gate(2));
          HS::frame.clocked[2] = g.rising; HS::frame.gate_high[2] = g.high; }
        { auto g = read_gate(kHemGateInD, busFrames, numFrames, v, prev_gate(3));
          HS::frame.clocked[3] = g.rising; HS::frame.gate_high[3] = g.high; }

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
            alg->left->Controller();
            alg->right->Controller();
        }

        write_frame_to_bus(kHemCvOutA, kHemCvOutAMode, HS::frame.outputs[0].value,
                           busFrames, numFrames, v);
        write_frame_to_bus(kHemCvOutB, kHemCvOutBMode, HS::frame.outputs[1].value,
                           busFrames, numFrames, v);
        write_frame_to_bus(kHemCvOutC, kHemCvOutCMode, HS::frame.outputs[2].value,
                           busFrames, numFrames, v);
        write_frame_to_bus(kHemCvOutD, kHemCvOutDMode, HS::frame.outputs[3].value,
                           busFrames, numFrames, v);
    }

    static bool draw(_NT_algorithm* self) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        if (!alg->started) return false;
        std::memset(NT_screen, 0, 128 * 64);
        HS::gfx_offset = 0;
        alg->left->DrawHeader();
        alg->left->View();
        HS::gfx_offset = 128;
        alg->right->DrawHeader();
        alg->right->View();
        HS::gfx_offset = 0;
        return true;
    }

    static uint32_t hasCustomUi(_NT_algorithm*) {
        return kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR;
    }

    static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        if (data.encoders[0] != 0) alg->left->OnEncoderMove(data.encoders[0]);
        if (data.encoders[1] != 0) alg->right->OnEncoderMove(data.encoders[1]);
        if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
            alg->left->OnButtonPress();
        }
        if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR)) {
            alg->right->OnButtonPress();
        }
    }

    static void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        uint64_t l = alg->left->OnDataRequest();
        uint64_t r = alg->right->OnDataRequest();
        stream.addMemberName("sel_l");        stream.addNumber((int)alg->cached_idx_left);
        stream.addMemberName("sel_r");        stream.addNumber((int)alg->cached_idx_right);
        stream.addMemberName("hem_left_hi");  stream.addNumber((int)(uint32_t)(l >> 32));
        stream.addMemberName("hem_left_lo");  stream.addNumber((int)(uint32_t)(l & 0xFFFFFFFFu));
        stream.addMemberName("hem_right_hi"); stream.addNumber((int)(uint32_t)(r >> 32));
        stream.addMemberName("hem_right_lo"); stream.addNumber((int)(uint32_t)(r & 0xFFFFFFFFu));
    }

    static bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
        auto* alg = static_cast<HemispheresInstance*>(self);
        int num_members = 0;
        if (!parse.numberOfObjectMembers(num_members)) return false;
        int sel_l = -1, sel_r = -1;
        int lhi = 0, llo = 0, rhi = 0, rlo = 0;
        bool got_lhi = false, got_llo = false, got_rhi = false, got_rlo = false;
        for (int i = 0; i < num_members; ++i) {
            if      (parse.matchName("sel_l"))        { if (!parse.number(sel_l)) return false; }
            else if (parse.matchName("sel_r"))        { if (!parse.number(sel_r)) return false; }
            else if (parse.matchName("hem_left_hi"))  { if (!parse.number(lhi)) return false; got_lhi = true; }
            else if (parse.matchName("hem_left_lo"))  { if (!parse.number(llo)) return false; got_llo = true; }
            else if (parse.matchName("hem_right_hi")) { if (!parse.number(rhi)) return false; got_rhi = true; }
            else if (parse.matchName("hem_right_lo")) { if (!parse.number(rlo)) return false; got_rlo = true; }
            else                                      { if (!parse.skipMember()) return false; }
        }
        if (sel_l >= 0 && sel_l < kAppletCount && (uint8_t)sel_l != alg->cached_idx_left) {
            hemispheres_swap(alg->left, alg->sram_left, (uint8_t)sel_l, HS::LEFT_HEMISPHERE);
            alg->cached_idx_left = (uint8_t)sel_l;
        }
        if (sel_r >= 0 && sel_r < kAppletCount && (uint8_t)sel_r != alg->cached_idx_right) {
            hemispheres_swap(alg->right, alg->sram_right, (uint8_t)sel_r, HS::RIGHT_HEMISPHERE);
            alg->cached_idx_right = (uint8_t)sel_r;
        }
        if (got_lhi && got_llo) {
            uint64_t l = ((uint64_t)(uint32_t)lhi << 32) | (uint64_t)(uint32_t)llo;
            alg->left->OnDataReceive(l);
        }
        if (got_rhi && got_rlo) {
            uint64_t r = ((uint64_t)(uint32_t)rhi << 32) | (uint64_t)(uint32_t)rlo;
            alg->right->OnDataReceive(r);
        }
        return true;
    }
};

}  // namespace hem_shim

#define NT_HEMISPHERES_PLUGIN(guid_str_4chars, name_str, desc_str) \
    static const _NT_factory _hemispheres_factory = { \
        .guid = NT_MULTICHAR(guid_str_4chars[0], guid_str_4chars[1], \
                             guid_str_4chars[2], guid_str_4chars[3]), \
        .name = name_str, \
        .description = desc_str, \
        .numSpecifications = 0, \
        .calculateRequirements = hem_shim::HemispheresShim::calculateRequirements, \
        .construct             = hem_shim::HemispheresShim::construct, \
        .step                  = hem_shim::HemispheresShim::step, \
        .draw                  = hem_shim::HemispheresShim::draw, \
        .tags                  = kNT_tagUtility, \
        .hasCustomUi           = hem_shim::HemispheresShim::hasCustomUi, \
        .customUi              = hem_shim::HemispheresShim::customUi, \
        .serialise             = hem_shim::HemispheresShim::serialise, \
        .deserialise           = hem_shim::HemispheresShim::deserialise, \
    }; \
    extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) { \
        switch (selector) { \
        case kNT_selector_version:      return kNT_apiVersionCurrent; \
        case kNT_selector_numFactories: return 1; \
        case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &_hemispheres_factory : nullptr); \
        } \
        return 0; \
    }
