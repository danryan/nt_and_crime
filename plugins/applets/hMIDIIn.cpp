// hMIDIIn per-applet plug-in: MIDI -> CV.
//
// MIDI arrives through the NT firmware midiMessage / midiRealtime factory
// callbacks (Layer 0c), which feed HS::frame.MIDIState.ProcessMIDIMsg via the
// per_applet_runtime::route_midi_* helpers. The vendor Controller() then copies
// the decoded mapping[].output / clock_run onto the two physical outputs.
//
// frame.MIDIState is per-TU; this TU both populates it (from the MIDI callbacks)
// and reads it (in Controller()), so the per-TU-globals constraint holds.
//
// 10x clocked-multiplier note (CLAUDE.md "Critical gotcha"): the runtime runs
// vendor Controller() ~10 times per buffer. hMIDIIn's Controller() reads only
// map.output and clock_run and never calls Clock()/Gate(), so the multiplier is
// inert here. The clock-realtime path advances state inside ProcessMIDIMsg
// (driven once per injected realtime byte), NOT inside Controller(), so clock
// counting is not multiplied. Coverage shape: state injection + round-trip.
#include "../../shim/include/HemisphereApplet.h"
#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifests/hMIDIIn.h"
#include "../../vendor/O_C-Phazerville/software/src/applets/hMIDIIn.h"
#include "_per_applet_runtime.h"

using Manifest = per_applet::hMIDIIn;

static constexpr int kNumParams =
    per_applet_runtime::base_parameter_count<Manifest>();

static _NT_parameter s_params[kNumParams];
static bool s_params_init = false;

struct _AppletInstance : public HemiPluginInterface {
    hMIDIIn applet;
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

static void parameterChanged_impl(_NT_algorithm* /*self*/, int /*p*/) {}

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

// MIDI receive callbacks (Layer 0c). hMIDIIn is a MIDI consumer.
static void midiMessage_impl(_NT_algorithm* self, uint8_t b0, uint8_t b1, uint8_t b2) {
    per_applet_runtime::route_midi_message(self, b0, b1, b2);
}

static void midiRealtime_impl(_NT_algorithm* self, uint8_t byte) {
    per_applet_runtime::route_midi_realtime(self, byte);
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
//
// midiMessage / midiRealtime sit between draw and hasCustomUi in the
// _NT_factory struct (api.h:454-466); designated initializers must keep that
// order (CLAUDE.md "_NT_factory designated-initializer order").
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
    .midiRealtime          = midiRealtime_impl,
    .midiMessage           = midiMessage_impl,
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
