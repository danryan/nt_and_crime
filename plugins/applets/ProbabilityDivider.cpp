// hemispheres_shim.h is included first: it brings in HemisphereApplet and
// all vendor applet headers (via HemispheresFactory.h). The vendor
// ProbabilityDivider.h must NOT be included again — vendor applet headers
// carry no include guards and would cause class redefinitions.
#include "../../shim/include/hemispheres_shim.h"
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/ProbabilityDivider.h"
#include "_per_applet_runtime.h"

// ManifestNS aliases the manifest struct (not the vendor class ProbabilityDivider).
namespace { using ManifestNS = per_applet::ProbabilityDivider; }

// Disambiguate: ::ProbabilityDivider is the vendor applet class. The struct
// per_applet::ProbabilityDivider is the manifest. The vendor class is in
// global scope; ManifestNS is the manifest struct.
using VendorApplet = ::ProbabilityDivider;

struct _AppletInstance : public HemiPluginInterface {
    VendorApplet applet;
    per_applet_runtime::PerInstanceState input_state;
};

static void on_encoder_turn_impl(_NT_algorithm* self, int dir) {
    static_cast<_AppletInstance*>(self)->applet.OnEncoderMove(dir);
}

static void on_button_press_impl(_NT_algorithm* /*self*/) {
    // ProbabilityDivider has no OnButtonPress override; no-op.
}

static void on_aux_button_impl(_NT_algorithm* /*self*/) {
    // No aux-button behaviour in the vendor applet; no-op.
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

static void parameterChanged_impl(_NT_algorithm* /*self*/, int /*param*/) {
}

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

static const _NT_factory s_factory = {
    .guid                      = ManifestNS::guid,
    .name                      = ManifestNS::name,
    .description               = ManifestNS::description,
    .calculateRequirements     = calculateRequirements_impl,
    .construct                 = construct_impl,
    .parameterChanged          = parameterChanged_impl,
    .step                      = step_impl,
    .draw                      = draw_impl,
    .hasCustomUi               = hasCustomUi_impl,
    .customUi                  = customUi_impl,
    .serialise                 = serialise_impl,
    .deserialise               = deserialise_impl,
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

// C-linkage accessors for tests. Both TUs compile into the same binary; the
// functions use the complete _AppletInstance type without requiring the test
// TU to include ProbabilityDivider.h (which would duplicate ProbLoopLinker::
// instance and shim globals).
extern "C" {

uint64_t get_pd_state(_NT_algorithm* alg) {
    return static_cast<_AppletInstance*>(alg)->applet.OnDataRequest();
}

void set_pd_state(_NT_algorithm* alg, uint64_t state) {
    static_cast<_AppletInstance*>(alg)->applet.OnDataReceive(state);
}

}  // extern "C"
