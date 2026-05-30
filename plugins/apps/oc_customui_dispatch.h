#pragma once

// Consolidated O_C app customUI dispatch. Single source of truth for the
// transport glue every per-app .cpp used to copy: build a ::UI::Event from a
// firmware control gesture, bridge it to the vendor OC::UI::Event the app
// handler expects, and mirror any setting edit back into the NT parameter
// store. The runtime gesture state machine (held_since, classify_release, the
// mapping table, idle reset) lives in _per_app_runtime.h; this header composes
// it into the full customUI entry point.
//
// This is the ONLY file that pulls vendor UI/ui_events.h. Core
// _per_app_runtime.h and harness/include/oc_ui_sim.h stay UI-free so they avoid
// the OC::UI::Event vs ::UI::Event ambiguity under `using namespace OC`. Only
// per-app .cpp TUs (which already include ui_events.h) include this header.

#include "_per_app_runtime.h"
#include "UI/ui_events.h"

namespace oc_runtime {

// Construct a ::UI::Event and dispatch it to the app's button handler. The
// handler is typed void(*)(const OC::UI::Event&) (OC_apps.h forward-declares
// OC::UI::Event); ::UI::Event is layout-identical, so the reinterpret_cast
// bridge is the foundation's documented idiom.
inline void emit_button(AppAlgorithm& alg, uint16_t oc_control, uint8_t ev_type) {
    ::UI::Event e(static_cast<::UI::EventType>(ev_type), oc_control, 0,
                  last_controls_of(alg));
    alg.app->HandleButtonEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

inline void emit_encoder(AppAlgorithm& alg, uint16_t oc_control, int delta) {
    ::UI::Event e(::UI::EVENT_ENCODER, oc_control, static_cast<int16_t>(delta),
                  last_controls_of(alg));
    alg.app->HandleEncoderEvent(reinterpret_cast<const OC::UI::Event&>(e));
}

// Mirror any app-side setting whose value diverged from the NT parameter store
// back into the store. Guarded by the construct-time sentinel (alg.alive) and a
// valid algorithm index, matching the runtime's parameterChanged guard.
// NT_setParameterFromUi indexes the GLOBAL parameter table, so the push target
// adds NT_parameterOffset(); alg.v is plug-in-relative, so the store compare
// uses base + s. Omitting the offset writes one global index low and the
// firmware re-applies the edit to the setting above the edited one.
inline void push_settings_to_params(AppAlgorithm& alg) {
    if (!alg.alive) return;
    const int32_t idx = NT_algorithmIndex(&alg);
    if (idx < 0) return;
    const int base = settings_param_base();
    const int n    = alg.settings_facade.num_settings;
    for (int s = 0; s < n; ++s) {
        const int v = alg.settings_facade.get_value(
            alg.settings_facade.instance, s);
        if (alg.v[base + s] != static_cast<int16_t>(v)) {
            NT_setParameterFromUi(static_cast<uint32_t>(idx),
                                  static_cast<uint32_t>(base + s) + NT_parameterOffset(),
                                  static_cast<int16_t>(v));
        }
    }
}

// Full customUI dispatch. Buttons emit on the release edge, classified short vs
// long by the runtime. map_long_press = true maps a long release to the vendor
// EVENT_BUTTON_LONG_PRESS the handler tests for (Harrington1200, FPART); false
// emits the raw classification (Low_rents, BBGEN, BYTEBEATGEN ignore
// LONG_RELEASE). Encoders emit one EVENT_ENCODER per non-zero delta. The push-
// back mirrors edits to the NT store. Runtime bookkeeping runs last so
// held_since / last_controls reflect the post-event state.
inline void dispatch_custom_ui(AppAlgorithm& alg, const _NT_uiData& data,
                               bool map_long_press) {
    if (!alg.app) return;

    int n = 0;
    const ControlMapping* tbl = button_mapping_table(n);
    const uint16_t edges = data.controls ^ data.lastButtons;

    for (int i = 0; i < n; ++i) {
        const uint16_t bit  = tbl[i].nt_bit;
        const int      bi   = bit_index(bit);
        const bool now_down = (data.controls & bit) != 0;
        if ((edges & bit) && !now_down) {
            const uint8_t ev = classify_release(&alg, bi);
            const uint8_t out =
                map_long_press
                    ? (ev == EVENT_BUTTON_LONG_RELEASE ? EVENT_BUTTON_LONG_PRESS
                                                       : EVENT_BUTTON_PRESS)
                    : ev;
            emit_button(alg, tbl[i].oc_control, out);
        }
    }

    if (data.encoders[0] != 0) emit_encoder(alg, OC::CONTROL_ENCODER_L, data.encoders[0]);
    if (data.encoders[1] != 0) emit_encoder(alg, OC::CONTROL_ENCODER_R, data.encoders[1]);

    push_settings_to_params(alg);
    customUi(alg, data);
}

// Function-pointer factory matching _NT_factory.customUi. The map_long_press
// flag is a template parameter so each app gets a plain function pointer.
template <bool MapLongPress>
inline void dispatch_custom_ui_factory(_NT_algorithm* self,
                                       const _NT_uiData& data) {
    dispatch_custom_ui(*static_cast<AppAlgorithm*>(self), data, MapLongPress);
}

}  // namespace oc_runtime
