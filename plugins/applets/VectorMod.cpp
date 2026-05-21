// Per-applet plug-in: VectorMod.
//
// Vendor: vendor/O_C-Phazerville/software/src/applets/VectorMod.h
// Manifest: shim/include/applet_manifests/VectorMod.h
//
// Vendor dep accounting: HSVectorOscillator.h and WaveformManager.h are
// header-only and live in the shim baseline (shim/include/vector_osc/).
// No .cpp link required; VENDOR_DEPS_VectorMod is empty in the Makefile.
//
// The manifest struct name (per_applet::VectorMod) collides with the vendor
// class name (VectorMod). Alias the manifest before the vendor header is
// included, then reference the vendor class as ::VectorMod in _AppletInstance.
//
// Bus layout (from vendor Controller/SetHelp):
//   Gate A -> Clock(0) -> osc[0].Start()
//   CV A   -> In(0) > HEMISPHERE_3V_CV -> osc[0].Cycle(cycle)
//   Gate B -> Clock(1) -> osc[1].Start()
//   CV B   -> In(1) > HEMISPHERE_3V_CV -> osc[1].Cycle(cycle)
//   Out A  <- osc[0].Next()
//   Out B  <- osc[1].Next()

#include "_per_applet_runtime.h"

#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/VectorMod.h"

// Alias manifest before the vendor class shadows the unqualified name.
using AppletManifest = per_applet::VectorMod;

// Vector-osc prereqs: DMAMEM, InterpLinear16, user_waveforms declaration.
// Must precede the vendor VectorMod.h (which does relative includes of these).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../shim/include/vector_osc/vec_osc_prereqs.h"
#include "../../shim/include/vector_osc/HSVectorOscillator.h"
#include "../../shim/include/vector_osc/WaveformManager.h"
#pragma GCC diagnostic pop

#include "../../vendor/O_C-Phazerville/software/src/applets/VectorMod.h"

struct _AppletInstance : public HemiPluginInterface {
    ::VectorMod applet;
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
    inst->applet.AuxButton();
}

static void calculateRequirements_impl(_NT_algorithmRequirements& req,
                                       const int32_t* /*specifications*/) {
    req.numParameters = per_applet_runtime::base_parameter_count<AppletManifest>();
    req.sram          = sizeof(_AppletInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& req,
                                     const int32_t* /*specifications*/) {
    static _NT_parameter params[per_applet_runtime::base_parameter_count<AppletManifest>()];
    per_applet_runtime::emit_base_parameters<AppletManifest>(params);

    auto* inst = new(ptrs.sram) _AppletInstance{};

    inst->parameters     = params;
    inst->parameterPages = nullptr;

    inst->magic              = kHemiInterfaceMagic;
    inst->interface_version  = kHemiInterfaceVersion;
    inst->render_view             = per_applet_runtime::render_view_with_offset<_AppletInstance>;
    inst->on_encoder_turn         = on_encoder_turn_impl;
    inst->on_encoder_turn_shifted = on_encoder_turn_impl;
    inst->on_button_press         = on_button_press_impl;
    inst->on_aux_button           = on_aux_button_impl;

    inst->applet.BaseStart(HS::LEFT_HEMISPHERE);

    (void)req;
    return inst;
}

static void parameterChanged_impl(_NT_algorithm* /*self*/, int /*param*/) {}

static void step_impl(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* inst = static_cast<_AppletInstance*>(self);
    per_applet_runtime::populate_frame_from_bus<AppletManifest>(self, busFrames, numFramesBy4, inst->input_state);
    per_applet_runtime::run_controller_inner_ticks(&inst->applet, numFramesBy4);
    per_applet_runtime::write_outputs_to_bus<AppletManifest>(self, busFrames, numFramesBy4);
}

static bool draw_impl(_NT_algorithm* self) {
    auto* p = static_cast<HemiPluginInterface*>(self);
    if (p->render_view) p->render_view(self, 0, 0);
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

static _NT_factory factory = {
    .guid        = AppletManifest::guid,
    .name        = AppletManifest::name,
    .description = AppletManifest::description,
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

// Test seam: extern "C" opaque accessors so the test TU can exercise
// OnDataRequest / OnDataReceive without including the vendor header (which
// would collide with the plugin TU definitions).
extern "C" uint64_t vectormod_on_data_request(_NT_algorithm* self) {
    auto* inst = static_cast<_AppletInstance*>(self);
    return inst->applet.OnDataRequest();
}

extern "C" void vectormod_on_data_receive(_NT_algorithm* self, uint64_t state) {
    auto* inst = static_cast<_AppletInstance*>(self);
    inst->applet.OnDataReceive(state);
}

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&factory : 0;
        default: break;
    }
    return 0;
}
