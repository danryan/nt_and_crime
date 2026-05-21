#pragma once

#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <cstdint>
#include <cstring>
#include <new>

#include "../../shim/include/HemiPluginInterface.h"
#include "../../shim/include/applet_manifest.h"
#include "../../shim/include/HemisphereApplet.h"
#include "../../shim/include/HSIOFrame.h"
#include "../../shim/include/OC_core.h"
#include "../../shim/include/HSClockManager.h"
#include "../../shim/include/hem_shim.h"

namespace per_applet_runtime {

// Parameter table layout for a per-applet plug-in:
//
//   [0 .. N_in - 1]                              input bus selectors
//   [N_in .. N_in + 2*N_out - 1]                 output bus + mode pairs
//   [N_in + 2*N_out .. N_in + 2*N_out + N_app - 1] applet-specific params
//
// Each output gets two adjacent parameters: the bus selector (named per
// the manifest) and a generic "Output mode" parameter. The UI shows them
// in sequence so the relationship is visually obvious.

template <typename ManifestNS>
constexpr int input_count() {
    return sizeof(ManifestNS::inputs) / sizeof(BusParam);
}

template <typename ManifestNS>
constexpr int output_count() {
    return sizeof(ManifestNS::outputs) / sizeof(BusParam);
}

template <typename ManifestNS>
constexpr int base_parameter_count() {
    return input_count<ManifestNS>() + 2 * output_count<ManifestNS>();
}

template <typename ManifestNS>
constexpr int first_applet_param_index() {
    return base_parameter_count<ManifestNS>();
}

inline uint8_t input_unit_for(BusKind kind) {
    return (kind == BusKind::audio) ? (uint8_t)kNT_unitAudioInput
                                    : (uint8_t)kNT_unitCvInput;
}

inline uint8_t output_unit_for(BusKind kind) {
    return (kind == BusKind::audio) ? (uint8_t)kNT_unitAudioOutput
                                    : (uint8_t)kNT_unitCvOutput;
}

// Maximum outputs supported per applet for the mode-name buffer below.
// Vendor Hemisphere applets have at most 2 outputs; Quadrants-class
// applets up to 4. Bump if a future applet exceeds this.
constexpr int kMaxOutputModeNames = 4;
constexpr int kMaxOutputModeNameLen = 24;

template <typename ManifestNS>
inline char (&mode_name_buffer())[kMaxOutputModeNames][kMaxOutputModeNameLen] {
    static char buf[kMaxOutputModeNames][kMaxOutputModeNameLen] = {{0}};
    return buf;
}

template <typename ManifestNS>
void emit_base_parameters(_NT_parameter* dst) {
    int idx = 0;
    constexpr int N_in  = input_count<ManifestNS>();
    constexpr int N_out = output_count<ManifestNS>();

    // Build "<output name> mode" into per-Manifest static storage so the
    // _NT_parameter::name pointer stays valid across calls. Each per-applet
    // plug-in calls emit_base_parameters once at construct() time.
    auto& mode_names = mode_name_buffer<ManifestNS>();
    for (int o = 0; o < N_out && o < kMaxOutputModeNames; ++o) {
        const char* base = ManifestNS::outputs[o].name;
        int i = 0;
        while (base[i] && i < kMaxOutputModeNameLen - 6) {
            mode_names[o][i] = base[i]; ++i;
        }
        const char suffix[] = " mode";
        for (int j = 0; suffix[j] && i < kMaxOutputModeNameLen - 1; ++j) {
            mode_names[o][i++] = suffix[j];
        }
        mode_names[o][i] = 0;
    }

    for (int i = 0; i < N_in; ++i) {
        const BusParam& p = ManifestNS::inputs[i];
        dst[idx++] = _NT_parameter{
            .name = p.name, .min = 0, .max = kNT_lastBus,
            .def = (int16_t)(1 + i), .unit = input_unit_for(p.kind),
            .scaling = 0, .enumStrings = nullptr,
        };
    }
    for (int o = 0; o < N_out; ++o) {
        const BusParam& p = ManifestNS::outputs[o];
        dst[idx++] = _NT_parameter{
            .name = p.name, .min = 0, .max = kNT_lastBus,
            .def = (int16_t)(13 + o), .unit = output_unit_for(p.kind),
            .scaling = 0, .enumStrings = nullptr,
        };
        const char* mname = (o < kMaxOutputModeNames) ? mode_names[o] : "Output mode";
        dst[idx++] = _NT_parameter{
            .name = mname, .min = 0, .max = 1, .def = 1,
            .unit = (uint8_t)kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr,
        };
    }
}

// Standalone per-applet runs as a single hemisphere (channel_offset = 0).
// Vendor HemisphereApplet base class reads Gate(ch) from
// HS::frame.clocked[ch + channel_offset()] and In(ch) from
// HS::frame.inputs[ch + channel_offset()]; both arrays are independent
// of each other and addressable on the same ch index. To match this,
// the runtime populates BOTH arrays for every manifest input,
// regardless of declared kind. The kind only affects the parameter
// unit (gate vs CV vs audio for the host UI) and the gate-edge state.
//
// Gate channels are assigned in manifest order across inputs of kind
// gate (gate_ch[0] = first manifest gate position). CV channels are
// assigned in manifest order across inputs of kind cv/audio (cv_ch[0]
// = first manifest CV position). This means an applet with manifest
// inputs [Gate, Gate, CV, CV] reads vendor Gate(0)/Gate(1) from the
// two gate inputs and vendor In(0)/In(1) from the two CV inputs - both
// indexed from 0 within their kind, mirroring the bundled Hemispheres
// host's behavior.
//
// PerInstanceState carries the per-applet input-edge state. Lives in
// _AppletInstance (NOT file-scope) so two instances of the same applet
// hosted side-by-side (e.g. Quadrants with two identical slots) keep
// independent rising-edge and changed-cv tracking.
struct PerInstanceState {
    int  last_cv[4]   = { 0, 0, 0, 0 };
    bool gate_prev[4] = { false, false, false, false };
};

template <typename ManifestNS>
void populate_frame_from_bus(_NT_algorithm* self,
                             float* busFrames, int numFramesBy4,
                             PerInstanceState& state) {
    int numFrames = numFramesBy4 * 4;
    const int16_t* v = self->v;
    constexpr int N_in = input_count<ManifestNS>();

    int gate_ch = 0;
    int cv_ch   = 0;
    for (int i = 0; i < N_in; ++i) {
        const BusParam& p = ManifestNS::inputs[i];
        if (p.kind == BusKind::gate) {
            auto g = hem_shim::read_gate(i, busFrames, numFrames, v, state.gate_prev[gate_ch]);
            HS::frame.clocked[gate_ch]   = g.rising;
            HS::frame.gate_high[gate_ch] = g.high;
            ++gate_ch;
        } else {
            hem_shim::copy_bus_to_frame(i, &HS::frame.inputs[cv_ch], busFrames, numFrames, v);
            int delta = HS::frame.inputs[cv_ch] - state.last_cv[cv_ch];
            if (delta < 0) delta = -delta;
            HS::frame.changed_cv[cv_ch] = (delta > 32);
            if (HS::frame.changed_cv[cv_ch]) state.last_cv[cv_ch] = HS::frame.inputs[cv_ch];
            ++cv_ch;
        }
    }
}

template <typename ManifestNS>
void write_outputs_to_bus(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    int numFrames = numFramesBy4 * 4;
    const int16_t* v = self->v;
    constexpr int N_in  = input_count<ManifestNS>();
    constexpr int N_out = output_count<ManifestNS>();

    for (int o = 0; o < N_out; ++o) {
        int bus_param  = N_in + 2 * o;
        int mode_param = N_in + 2 * o + 1;
        hem_shim::write_frame_to_bus(bus_param, mode_param,
                                     HS::frame.outputs[o].value,
                                     busFrames, numFrames, v);
    }
}

// Inner-tick loop matching the bundled host shim at
// shim/include/hemispheres_shim.h:179. Honors hem_shim::inner_ticks_override
// for the host-side time-injection helper used in tests.
template <typename Applet>
void run_controller_inner_ticks(Applet* applet, int numFramesBy4) {
    int numFrames = numFramesBy4 * 4;
    int ticks_this_step;
    if (hem_shim::inner_ticks_override > 0) {
        ticks_this_step = hem_shim::inner_ticks_override;
        hem_shim::inner_ticks_override = 0;
    } else {
        ticks_this_step = numFrames / 3;
        if (ticks_this_step < 1) ticks_this_step = 1;
    }
    for (int i = 0; i < ticks_this_step; ++i) {
        OC::CORE::ticks += 1;
        clock_m.advance_one_tick();
        for (int ch = 0; ch < 4; ++ch) {
            if (HS::frame.clock_countdown[ch] > 0) {
                if (--HS::frame.clock_countdown[ch] == 0) {
                    HS::frame.outputs[ch].set(0);
                }
            }
        }
        applet->Controller();
    }
}

// render_view_with_offset: templated helper that sets HS::gfx_offset and
// HS::gfx_offset_y, draws the applet header (mirrors bundled
// hemispheres_shim's pre-View `DrawHeader()` call), then delegates to
// the applet's View(). Clears both offsets back to 0 on exit.
//
// Per-applet plug-ins wire inst->render_view to this helper instead of
// writing their own render_view_impl. Hosts pass the slot origin via
// origin_x / origin_y; standalone use leaves both at 0. The header
// call is required because vendor applet View() bodies do NOT render
// their own name (the bundled Hemispheres host code draws it for them
// at hemispheres_shim.h:210-213).
template <typename AppletInstance>
inline void render_view_with_offset(_NT_algorithm* self, int origin_x, int origin_y) {
    HS::gfx_offset   = origin_x;
    HS::gfx_offset_y = origin_y;
    auto* inst = static_cast<AppletInstance*>(self);
    inst->applet.DrawHeader();
    inst->applet.View();
    HS::gfx_offset   = 0;
    HS::gfx_offset_y = 0;
}

// customUi routing through the HemiPluginInterface pointers populated by
// construct(). Standalone path is identical to what the host would do.
inline void route_custom_ui(_NT_algorithm* self, const _NT_uiData& data) {
    auto* p = static_cast<HemiPluginInterface*>(self);
    if (data.encoders[0] != 0 && p->on_encoder_turn) {
        p->on_encoder_turn(self, data.encoders[0]);
    }
    if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
        if (p->on_button_press) p->on_button_press(self);
    }
    if ((data.controls & kNT_button1) && !(data.lastButtons & kNT_button1)) {
        if (p->on_aux_button) p->on_aux_button(self);
    }
}

// serialise / deserialise wrappers around vendor Hemisphere
// OnDataRequest / OnDataReceive. Packs/unpacks the uint64_t state under
// the JSON members "hemi_hi" + "hemi_lo".
template <typename Applet>
void write_data_request(Applet* applet, _NT_jsonStream& stream) {
    uint64_t state = applet->OnDataRequest();
    stream.addMemberName("hemi_hi"); stream.addNumber((int)(uint32_t)(state >> 32));
    stream.addMemberName("hemi_lo"); stream.addNumber((int)(uint32_t)(state & 0xFFFFFFFFu));
}

template <typename Applet>
bool read_data_receive(Applet* applet, _NT_jsonParse& parse) {
    int num_members = 0;
    if (!parse.numberOfObjectMembers(num_members)) return false;
    int hi = 0, lo = 0;
    bool got_hi = false, got_lo = false;
    for (int i = 0; i < num_members; ++i) {
        if      (parse.matchName("hemi_hi")) { if (!parse.number(hi)) return false; got_hi = true; }
        else if (parse.matchName("hemi_lo")) { if (!parse.number(lo)) return false; got_lo = true; }
        else                                  { if (!parse.skipMember()) return false; }
    }
    if (got_hi && got_lo) {
        uint64_t state = ((uint64_t)(uint32_t)hi << 32) | (uint64_t)(uint32_t)lo;
        applet->OnDataReceive(state);
    }
    return true;
}

}  // namespace per_applet_runtime
