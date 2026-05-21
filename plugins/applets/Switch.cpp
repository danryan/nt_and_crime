#include "../../shim/include/HemisphereApplet.h"
#include "../../shim/include/PhzIcons.h"
#include "../../shim/include/Arduino.h"
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/Switch.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/Switch.h"
#include "_per_applet_runtime.h"

namespace { using ManifestNS = per_applet::Switch; }

struct _AppletInstance : public HemiPluginInterface {
    ::Switch applet;
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

// Switch has a mixed 4-input manifest [Clock(gate), Gate(gate), CV1(cv), CV2(cv)].
// populate_frame_from_bus maps manifest position i to frame.clocked[i]/gate_high[i]
// for gate inputs and frame.inputs[i] for cv inputs. That would place the cv signals
// at inputs[2] and inputs[3], but the vendor applet reads In(0)=inputs[0] and
// In(1)=inputs[1]. We wire the mapping manually so each signal lands at the correct
// frame index.
static void step_impl(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* inst        = static_cast<_AppletInstance*>(self);
    const int16_t* v  = self->v;
    int numFrames     = numFramesBy4 * 4;

    // Manifest position 0 = Clock (gate) -> clocked[0] + gate_high[0]
    {
        auto g = hem_shim::read_gate(0, busFrames, numFrames, v,
                                     inst->input_state.gate_prev[0]);
        HS::frame.clocked[0]   = g.rising;
        HS::frame.gate_high[0] = g.high;
    }
    // Manifest position 1 = Gate (gate) -> clocked[1] + gate_high[1]
    {
        auto g = hem_shim::read_gate(1, busFrames, numFrames, v,
                                     inst->input_state.gate_prev[1]);
        HS::frame.clocked[1]   = g.rising;
        HS::frame.gate_high[1] = g.high;
    }
    // Manifest position 2 = CV1 (cv) -> inputs[0]   (vendor reads In(0))
    hem_shim::copy_bus_to_frame(2, &HS::frame.inputs[0], busFrames, numFrames, v);
    // Manifest position 3 = CV2 (cv) -> inputs[1]   (vendor reads In(1))
    hem_shim::copy_bus_to_frame(3, &HS::frame.inputs[1], busFrames, numFrames, v);

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

// Test seam: Switch::OnDataRequest() always returns 0 (no serialisable state).
// Defined here for consistency with other per-applet plug-ins.
uint64_t switch_applet_on_data_request(_NT_algorithm* self) {
    return static_cast<_AppletInstance*>(self)->applet.OnDataRequest();
}
