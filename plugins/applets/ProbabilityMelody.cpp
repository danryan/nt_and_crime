// Shim headers must precede the vendor applet header so that HemisphereApplet,
// Arduino.h stubs (constrain, random), OC_core.h, and PhzIcons are all in
// scope when ProbabilityMelody.h (and its dep HSProbLoopLinker.h) is parsed.
#include "../../shim/include/HemisphereApplet.h"
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/ProbabilityMelody.h"

// ProbabilityMelody.h is the only vendor applet that includes
// "../HemisphereApplet.h" directly (from the vendor src/ tree). That file uses
// both #pragma once AND the named guard _HEM_APPLET_H_. The shim's
// HemisphereApplet.h uses only #pragma once, which tracks physical file path
// and cannot suppress the vendor copy. Defining _HEM_APPLET_H_ here poisons
// the vendor copy's #ifndef guard so its body is skipped. This is the
// established include-guard-poison technique (CLAUDE.md) applied per-applet-
// .cpp because only ProbabilityMelody.h exhibits this pattern.
#ifndef _HEM_APPLET_H_
#define _HEM_APPLET_H_
#endif

// HSicons.h poisoning:
// ProbabilityMelody's DrawParams uses UP_TRI_L_HALF and UP_TRI_R_HALF, defined
// inline in vendor HSicons.h (inside the HS_ICON_SET guard). Because the
// vendor HemisphereApplet.h body is poisoned above, HSicons.h is not pulled
// from the vendor chain. The shim HSicons.h does not declare these two icons
// (they are unique to ProbabilityMelody among the shipped applet set). Define
// them here before including the vendor applet so the TU sees them. The
// HS_ICON_SET guard prevents the vendor HSicons.h from redefining them if it
// is later included via a different path.
#ifndef HS_ICON_SET
// Only define the two missing icons; do not define the full guard so that if
// the vendor HSicons.h is somehow included the rest of its constants are still
// available. However, define these two ahead of time via a specific guard.
#endif
static const uint8_t UP_TRI_L_HALF[]
    = {0x00, 0x08, 0x0c, 0x0e, 0x00, 0x00, 0x00, 0x00};
static const uint8_t UP_TRI_R_HALF[]
    = {0x00, 0x00, 0x00, 0x0e, 0x0c, 0x08, 0x00, 0x00};

#include "../../vendor/O_C-Phazerville/software/src/applets/ProbabilityMelody.h"
#include "_per_applet_runtime.h"

// Use a distinct alias to avoid shadowing the ProbabilityMelody applet class.
using Manifest = per_applet::ProbabilityMelody;

// Number of base parameters: 2 inputs + 2 outputs * 2 = 6.
static constexpr int kNumParams = 2 + 2 * 2;

static _NT_parameter s_params[kNumParams];
static bool s_params_init = false;

struct _AppletInstance : public HemiPluginInterface {
    ProbabilityMelody applet;
    per_applet_runtime::PerInstanceState input_state;
};

static void on_encoder_turn_impl(_NT_algorithm* self, int dir) {
    static_cast<_AppletInstance*>(self)->applet.OnEncoderMove(dir);
}

static void on_button_press_impl(_NT_algorithm* /*self*/) {
    // ProbabilityMelody has no OnButtonPress override; no-op.
}

static void on_aux_button_impl(_NT_algorithm* /*self*/) {
    // No aux-button behaviour in the vendor applet; no-op.
}

// ---------------------------------------------------------------------------
// _NT_factory hooks
// ---------------------------------------------------------------------------

static void calculateRequirements_impl(_NT_algorithmRequirements& req,
                                       const int32_t* /*specifications*/) {
    req.numParameters = kNumParams;
    req.sram  = sizeof(_AppletInstance);
    req.dram  = 0;
    req.dtc   = 0;
    req.itc   = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& /*req*/,
                                     const int32_t* /*specifications*/) {
    auto* inst = new (ptrs.sram) _AppletInstance();

    if (!s_params_init) {
        per_applet_runtime::emit_base_parameters<Manifest>(s_params);
        s_params_init = true;
    }

    inst->parameters     = s_params;
    inst->parameterPages = nullptr;
    const_cast<int16_t*&>(inst->v) = nullptr;

    inst->magic             = kHemiInterfaceMagic;
    inst->interface_version = kHemiInterfaceVersion;
    inst->render_view             = per_applet_runtime::render_view_with_offset<_AppletInstance>;
    inst->on_encoder_turn         = on_encoder_turn_impl;
    inst->on_encoder_turn_shifted = on_encoder_turn_impl;
    inst->on_button_press         = on_button_press_impl;
    inst->on_aux_button           = on_aux_button_impl;

    inst->applet.BaseStart(HS::LEFT_HEMISPHERE);
    return inst;
}

static void parameterChanged_impl(_NT_algorithm* /*self*/, int /*p*/) {
    // No applet-specific parameter remapping for ProbabilityMelody.
}

static void step_impl(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* inst = static_cast<_AppletInstance*>(self);
    per_applet_runtime::populate_frame_from_bus<Manifest>(self, busFrames, numFramesBy4, inst->input_state);
    per_applet_runtime::run_controller_inner_ticks(&inst->applet, numFramesBy4);
    per_applet_runtime::write_outputs_to_bus<Manifest>(self, busFrames, numFramesBy4);
}

static bool draw_impl(_NT_algorithm* self) {
    auto* p = static_cast<HemiPluginInterface*>(self);
    if (p->render_view) p->render_view(self, 0, 0);
    return true;
}

static uint32_t hasCustomUi_impl(_NT_algorithm* /*self*/) {
    return kNT_encoderL | kNT_encoderButtonL;
}

static void customUi_impl(_NT_algorithm* self, const _NT_uiData& data) {
    per_applet_runtime::route_custom_ui(self, data);
}

static void serialise_impl(_NT_algorithm* self, _NT_jsonStream& stream) {
    auto* inst = static_cast<_AppletInstance*>(self);
    per_applet_runtime::write_data_request(&inst->applet, stream);
}

static bool deserialise_impl(_NT_algorithm* self, _NT_jsonParse& parse) {
    auto* inst = static_cast<_AppletInstance*>(self);
    return per_applet_runtime::read_data_receive(&inst->applet, parse);
}

// ---------------------------------------------------------------------------
// Factory and plug-in entry point
// ---------------------------------------------------------------------------

static const _NT_factory factory = {
    .guid        = Manifest::guid,
    .name        = Manifest::name,
    .description = Manifest::description,
    .calculateRequirements = calculateRequirements_impl,
    .construct             = construct_impl,
    .parameterChanged      = parameterChanged_impl,
    .step                  = step_impl,
    .draw                  = draw_impl,
    .hasCustomUi           = hasCustomUi_impl,
    .customUi              = customUi_impl,
    .serialise             = serialise_impl,
    .deserialise           = deserialise_impl,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&factory : 0;
        default: break;
    }
    return 0;
}

// C-linkage accessors for tests. Both TUs compile into the same binary; the
// functions use the complete _AppletInstance type without requiring the test
// TU to include ProbabilityMelody.h (which would duplicate ProbLoopLinker::
// instance and shim globals).
extern "C" {

uint64_t get_pm_state(_NT_algorithm* alg) {
    return static_cast<_AppletInstance*>(alg)->applet.OnDataRequest();
}

void set_pm_state(_NT_algorithm* alg, uint64_t state) {
    static_cast<_AppletInstance*>(alg)->applet.OnDataReceive(state);
}

}  // extern "C"
