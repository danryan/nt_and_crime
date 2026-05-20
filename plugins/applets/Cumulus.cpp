// Shim headers that vendor Cumulus.h requires must come first.
#include "../../shim/include/HemisphereApplet.h"
#include "../../shim/include/PhzIcons.h"
#include "../../shim/include/Arduino.h"

#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/Cumulus.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/Cumulus.h"
#include "_per_applet_runtime.h"

namespace { using ManifestNS = per_applet::Cumulus; }

struct _AppletInstance : public HemiPluginInterface {
    Cumulus applet;
};

// ---------------------------------------------------------------------------
// HemiPluginInterface render / input function implementations.
// ---------------------------------------------------------------------------

static void render_view_impl(_NT_algorithm* self, int origin_x, int origin_y) {
    (void)origin_y;
    HS::gfx_offset = origin_x;
    static_cast<_AppletInstance*>(self)->applet.View();
    HS::gfx_offset = 0;
}

static void on_encoder_turn_impl(_NT_algorithm* self, int dir) {
    static_cast<_AppletInstance*>(self)->applet.OnEncoderMove(dir);
}

static void on_button_press_impl(_NT_algorithm* self) {
    static_cast<_AppletInstance*>(self)->applet.OnButtonPress();
}

static void on_aux_button_impl(_NT_algorithm* self) {
    // Cumulus has no distinct aux-button action; mirror button press.
    static_cast<_AppletInstance*>(self)->applet.OnButtonPress();
}

// ---------------------------------------------------------------------------
// _NT_factory hooks.
// ---------------------------------------------------------------------------

static void calculateRequirements_impl(_NT_algorithmRequirements& req,
                                        const int32_t* /*specifications*/) {
    req.numParameters = per_applet_runtime::base_parameter_count<ManifestNS>();
    req.sram          = sizeof(_AppletInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

static _NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                                      const _NT_algorithmRequirements& /*req*/,
                                      const int32_t* /*specifications*/) {
    auto* inst = new (ptrs.sram) _AppletInstance();

    // Base-class _NT_algorithm fields. v[] is wired by the host/harness after
    // construct() returns; do not allocate it here.
    static _NT_parameter s_params[per_applet_runtime::base_parameter_count<ManifestNS>()];
    per_applet_runtime::emit_base_parameters<ManifestNS>(s_params);
    inst->parameters     = s_params;
    inst->parameterPages = nullptr;

    // HemiPluginInterface ABI fields.
    inst->magic             = kHemiInterfaceMagic;
    inst->interface_version = kHemiInterfaceVersion;
    inst->render_view              = render_view_impl;
    inst->on_encoder_turn          = on_encoder_turn_impl;
    inst->on_encoder_turn_shifted  = on_encoder_turn_impl;
    inst->on_button_press          = on_button_press_impl;
    inst->on_aux_button            = on_aux_button_impl;

    // Start the applet on the left hemisphere (standalone = offset 0).
    inst->applet.BaseStart(HS::LEFT_HEMISPHERE);

    return inst;
}

static void parameterChanged_impl(_NT_algorithm* /*self*/, int /*p*/) {
    // No applet-specific parameter reaction needed for Cumulus.
}

static void step_impl(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* inst = static_cast<_AppletInstance*>(self);
    per_applet_runtime::populate_frame_from_bus<ManifestNS>(self, busFrames, numFramesBy4);
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

// ---------------------------------------------------------------------------
// Accessor for test binaries (same link unit).
// ---------------------------------------------------------------------------

Cumulus* get_cumulus_applet(_NT_algorithm* self) {
    return &static_cast<_AppletInstance*>(self)->applet;
}

// ---------------------------------------------------------------------------
// _NT_factory registration.
// ---------------------------------------------------------------------------

static _NT_factory s_factory = {
    .guid                    = ManifestNS::guid,
    .name                    = ManifestNS::name,
    .description             = ManifestNS::description,
    .numSpecifications       = 0,
    .calculateRequirements   = calculateRequirements_impl,
    .construct               = construct_impl,
    .parameterChanged        = parameterChanged_impl,
    .step                    = step_impl,
    .draw                    = draw_impl,
    .tags                    = kNT_tagUtility,
    .hasCustomUi             = hasCustomUi_impl,
    .customUi                = customUi_impl,
    .serialise               = serialise_impl,
    .deserialise             = deserialise_impl,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:
            return data == 0 ? reinterpret_cast<uintptr_t>(&s_factory) : 0;
        default:
            return 0;
    }
}
