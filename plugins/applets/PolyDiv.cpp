#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/PolyDiv.h"
#include "_per_applet_runtime.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/PolyDiv.h"

namespace { using ManifestNS = per_applet::PolyDiv; }

struct _AppletInstance : public HemiPluginInterface {
    PolyDiv applet;
    per_applet_runtime::PerInstanceState input_state;
};

static void on_encoder_turn_impl(_NT_algorithm* self, int dir) {
    static_cast<_AppletInstance*>(self)->applet.OnEncoderMove(dir);
}

static void on_button_press_impl(_NT_algorithm* self) {
    static_cast<_AppletInstance*>(self)->applet.OnButtonPress();
}

static void on_aux_button_impl(_NT_algorithm* self) {
    static_cast<_AppletInstance*>(self)->applet.OnButtonPress();
}

static constexpr int kParamCount = per_applet_runtime::base_parameter_count<ManifestNS>();
static _NT_parameter s_parameters[kParamCount];

static void calculateRequirements_impl(_NT_algorithmRequirements& req,
                                       const int32_t* /*specifications*/) {
    req.numParameters = per_applet_runtime::base_parameter_count<ManifestNS>();
    req.sram          = static_cast<uint32_t>(sizeof(_AppletInstance));
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& /*req*/,
                                     const int32_t* /*specifications*/) {
    auto* inst = new (ptrs.sram) _AppletInstance{};

    inst->magic                   = kHemiInterfaceMagic;
    inst->interface_version       = kHemiInterfaceVersion;
    inst->render_view             = per_applet_runtime::render_view_with_offset<_AppletInstance>;
    inst->on_encoder_turn         = on_encoder_turn_impl;
    inst->on_encoder_turn_shifted = on_encoder_turn_impl;
    inst->on_button_press         = on_button_press_impl;
    inst->on_aux_button           = on_aux_button_impl;

    per_applet_runtime::emit_base_parameters<ManifestNS>(s_parameters);
    inst->parameters = s_parameters;

    inst->applet.BaseStart(HS::LEFT_HEMISPHERE);

    return inst;
}

static void parameterChanged_impl(_NT_algorithm* /*self*/, int /*param*/) {}

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

static const _NT_factory s_factory = {
    .guid                  = ManifestNS::guid,
    .name                  = ManifestNS::name,
    .description           = ManifestNS::description,
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
        case kNT_selector_factoryInfo:
            if (data == 0) return reinterpret_cast<uintptr_t>(&s_factory);
            return 0;
        default: return 0;
    }
}

// C-linkage test seam: opaque accessors for divider state. Both TUs compile
// into the same binary. Tests do NOT include PolyDiv.h directly (ODR
// discipline). State introspection is via these accessors plus serialise/
// deserialise.
extern "C" {

uint64_t get_polydiv_state(_NT_algorithm* alg) {
    return static_cast<_AppletInstance*>(alg)->applet.OnDataRequest();
}

void set_polydiv_state(_NT_algorithm* alg, uint64_t state) {
    static_cast<_AppletInstance*>(alg)->applet.OnDataReceive(state);
}

// Per-divider state inspection: returns the clock_count of divider[ch] (0..3).
uint8_t get_polydiv_clock_count(_NT_algorithm* alg, int ch) {
    return static_cast<_AppletInstance*>(alg)->applet.divider[ch].clock_count;
}

// Per-divider state inspection: returns the steps of divider[ch] (0..63).
uint8_t get_polydiv_steps(_NT_algorithm* alg, int ch) {
    return static_cast<_AppletInstance*>(alg)->applet.divider[ch].steps;
}

}  // extern "C"
