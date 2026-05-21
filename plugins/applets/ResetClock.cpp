#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/ResetClock.h"
#include "_per_applet_runtime.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/ResetClock.h"

namespace { using ManifestNS = per_applet::ResetClock; }

struct _AppletInstance : public HemiPluginInterface {
    ResetClock applet;
    per_applet_runtime::PerInstanceState input_state;
};

static void on_encoder_turn_impl(_NT_algorithm* self, int dir) {
    auto* inst = static_cast<_AppletInstance*>(self);
    inst->applet.OnEncoderMove(dir);
}

static void on_button_press_impl(_NT_algorithm* self) {
    auto* inst = static_cast<_AppletInstance*>(self);
    inst->applet.OnButtonPress();
}

static void on_aux_button_impl(_NT_algorithm* self) {
    auto* inst = static_cast<_AppletInstance*>(self);
    inst->applet.OnButtonPress();
}

static void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = per_applet_runtime::base_parameter_count<ManifestNS>();
    req.sram          = sizeof(_AppletInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& req,
                                     const int32_t*) {
    auto* inst = new (ptrs.sram) _AppletInstance();

    static _NT_parameter params[per_applet_runtime::base_parameter_count<ManifestNS>()];
    per_applet_runtime::emit_base_parameters<ManifestNS>(params);
    inst->parameters     = params;
    inst->parameterPages = nullptr;

    inst->magic             = kHemiInterfaceMagic;
    inst->interface_version = kHemiInterfaceVersion;
    inst->render_view              = per_applet_runtime::render_view_with_offset<_AppletInstance>;
    inst->on_encoder_turn          = on_encoder_turn_impl;
    inst->on_encoder_turn_shifted  = on_encoder_turn_impl;
    inst->on_button_press          = on_button_press_impl;
    inst->on_aux_button            = on_aux_button_impl;

    inst->applet.BaseStart(LEFT_HEMISPHERE);
    return inst;
}

static void parameterChanged_impl(_NT_algorithm*, int) {}

static void step_impl(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* inst = static_cast<_AppletInstance*>(self);
    per_applet_runtime::populate_frame_from_bus<ManifestNS>(self, busFrames, numFramesBy4, inst->input_state);
    per_applet_runtime::run_controller_inner_ticks(&inst->applet, numFramesBy4);
    per_applet_runtime::write_outputs_to_bus<ManifestNS>(self, busFrames, numFramesBy4);
}

static bool draw_impl(_NT_algorithm* self) {
    static_cast<HemiPluginInterface*>(self)->render_view(self, 0, 0);
    return true;
}

static uint32_t hasCustomUi_impl(_NT_algorithm*) {
    return kNT_encoderL | kNT_encoderButtonL | kNT_button1;
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

static _NT_factory factory = {
    .guid        = ManifestNS::guid,
    .name        = ManifestNS::name,
    .description = ManifestNS::description,
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
    }
    return 0;
}

// Test seam: returns the vendor applet's OnDataRequest() value.
// Only length, offset, and spacing are serialised (pending_clocks and
// position are runtime state not persisted by the vendor applet).
uint64_t resetclock_applet_on_data_request(_NT_algorithm* self) {
    return static_cast<_AppletInstance*>(self)->applet.OnDataRequest();
}

// Test seam: injects state via vendor applet's OnDataReceive().
void resetclock_applet_on_data_receive(_NT_algorithm* self, uint64_t data) {
    static_cast<_AppletInstance*>(self)->applet.OnDataReceive(data);
}
