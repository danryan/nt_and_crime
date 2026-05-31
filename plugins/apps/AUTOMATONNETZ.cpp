// Automatonnetz: O_C APP_AUTOMATONNETZ port. A vector-sequencer that walks a
// 5x5 grid of neo-Riemannian transform cells; each clock steps the walker by
// (dx, dy), lands on a cell, applies that cell's transform/transpose/inversion
// to a shared TonnetzState, and outputs the resulting triad as 1V/oct pitch on
// the four DAC channels. Same tonnetz engine as Harrington 1200.
//
// One NT plug-in compiling to one small .o. It embeds the vendor OC::App thunk
// table built from the Automatonnetz_* thunks, points OC::apps::current_app at
// it, and drives it through the shared per-app runtime. The vendor app body
// (the AutomatonnetzState / TransformCell SettingsBase classes, the CellGrid
// walker, the ISR, the menu/screensaver draw code, the event handlers) compiles
// unmodified.
//
// Structure follows plugins/apps/Harrington1200.cpp (the single-instance
// template): the vendor app owns the file-scope `automatonnetz_state` singleton
// (itself a SettingsBase), so the settings facade points at it.
//
// Settings model (grid subset, user-approved): only the 7 global GRID settings
// are NT parameters (passed as num_settings to oc_runtime::construct). The
// 25-cell transform grid (100 settings) stays app-internal, edited through the
// vendor 2D grid customUI and persisted whole through the Save/Restore blob
// (Automatonnetz_save packs all 25 cells after the grid settings). The cells are
// a spatial editor and do not linearize into a flat NT parameter page.

// Aggregation trigger: defining this BEFORE the runtime include pulls the OC
// shim impl into this single TU (see _per_app_runtime.h).
#define NT_OC_APP_TU 1

#include "_per_app_runtime.h"
#include "oc_customui_dispatch.h"

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "OC_config.h"
#include "OC_strings.h"
#include "OC_menus.h"
#include "OC_bitmaps.h"
#include "OC_scales.h"            // OC::SemitoneQuantizer (vendor, not shadowed)
#include "Arduino.h"
#include "hem_graphics.h"
#include "util/util_settings.h"
// Pull the shim shadow of util/util_sync.h BEFORE the vendor app header. The
// vendor header quote-includes "util/util_sync.h" from inside the vendor tree;
// pulling the shim shadow first defines the UTIL_SYNC_H_ guard so the vendor
// sibling (ARM CMSIS exclusive-access intrinsics, <arm_math.h>) self-suppresses
// and the portable shim lock is used on host and ARM.
#include "util/util_sync.h"
#include "UI/ui_events.h"          // ::UI::Event for the customUi emit glue

#include "../../shim/include/oc_app_manifests/AUTOMATONNETZ.h"

#include <distingnt/api.h>
#include <new>

// Bind bare `menu::` (the vendor app uses menu::ScreenCursor / menu::kScreenLines)
// to the shim's OC::menu without dragging OC::UI into global scope (which would
// make the vendor app's `UI::Event` ambiguous against ::UI from ui_events.h).
namespace menu = OC::menu;

// Enable the vendor app body and pull it in. APP_AUTOMATONNETZ.h is guarded by
// ENABLE_APP_AUTOMATONNETZ; defining it before the include compiles the
// AutomatonnetzState / TransformCell settings classes, the file-scope
// `automatonnetz_state` singleton, and the Automatonnetz_* thunks into this TU.
#define ENABLE_APP_AUTOMATONNETZ 1
#include "APP_AUTOMATONNETZ.h"

namespace {

using ManifestNS = oc_app::AUTOMATONNETZ;

struct AutomatonnetzInstance : public oc_runtime::AppAlgorithm {};

AutomatonnetzInstance* g_instance = nullptr;

using OcEventFn = void (*)(const OC::UI::Event&);

// The App aggregate. Field order matches OC::App (OC_apps.h).
const OC::App the_automatonnetz_app = {
    /* id */                static_cast<uint16_t>(ManifestNS::guid & 0xFFFF),
    /* name */              ManifestNS::name,
    /* Init */              Automatonnetz_init,
    /* storageSize */       Automatonnetz_storageSize,
    /* Save */              Automatonnetz_save,
    /* Restore */           Automatonnetz_restore,
    /* HandleAppEvent */    Automatonnetz_handleAppEvent,
    /* loop */              Automatonnetz_loop,
    /* DrawMenu */          Automatonnetz_menu,
    /* DrawScreensaver */   Automatonnetz_screensaver,
    /* HandleButtonEvent */ reinterpret_cast<OcEventFn>(Automatonnetz_handleButtonEvent),
    /* HandleEncoderEvent */reinterpret_cast<OcEventFn>(Automatonnetz_handleEncoderEvent),
    /* isr */               Automatonnetz_isr,
};

void calculateRequirements_impl(_NT_algorithmRequirements& req, const int32_t*) {
    // numParameters MUST equal the actual populated range: the 12 I/O routing
    // rows plus one row per exposed setting. Only the 7 GRID settings are
    // exposed (grid-subset model), NOT the 100 cell settings.
    req.numParameters = oc_runtime::kIoParamCount + GRID_SETTING_LAST;
    req.sram          = sizeof(AutomatonnetzInstance);
    req.dram          = 0;
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct_impl(const _NT_algorithmMemoryPtrs& ptrs,
                              const _NT_algorithmRequirements&,
                              const int32_t*) {
    auto* inst = new (ptrs.sram) AutomatonnetzInstance();
    g_instance = inst;

    // Automatonnetz_init() (fired inside construct via app->Init) inits the grid
    // (cells_ + the CellGrid walker bound to cells_), the tonnetz state, and
    // randomizes the cell transforms.
    //
    // The default facade would route get/set, value_attr AND the persistence
    // blob through automatonnetz_state's SettingsBase, which covers only the 7
    // GRID settings. That is correct for the NT parameter rows (grid subset),
    // but it would drop the 25-cell grid on save/restore: the cells live in a
    // separate member array, persisted by the Automatonnetz_save/restore thunks,
    // not by automatonnetz_state.Save. Build the standard 7-grid-setting facade,
    // then override the blob hooks to the full app Save/Restore so the whole
    // state (grid settings + all 25 cells) survives a preset save/reload while
    // only the 7 grid settings remain NT parameters. The full blob
    // (Automatonnetz_storageSize ~= 7 + 25*4 bytes) stays well under
    // oc_runtime::kMaxBlobBytes.
    oc_runtime::SettingsFacade facade =
        oc_runtime::make_facade(&automatonnetz_state);
    facade.save         = [](void* /*self*/, void* blob) -> size_t {
        return Automatonnetz_save(blob);
    };
    facade.restore      = [](void* /*self*/, const void* blob) -> size_t {
        return Automatonnetz_restore(blob);
    };
    facade.storage_size = []() -> size_t { return Automatonnetz_storageSize(); };

    oc_runtime::construct_with_facade(*inst, &the_automatonnetz_app, facade,
                                      GRID_SETTING_LAST);
    return inst;
}

// Field order follows _NT_factory (api.h:468): tags BEFORE hasCustomUi/customUi.
const _NT_factory factory = {
    .guid        = ManifestNS::guid,
    .name        = ManifestNS::name,
    .description = ManifestNS::description,
    .calculateRequirements = calculateRequirements_impl,
    .construct             = construct_impl,
    .parameterChanged      = oc_runtime::parameterChanged_factory,
    .step                  = oc_runtime::step_factory,
    .draw                  = oc_runtime::draw_factory,
    .tags                  = kNT_tagUtility,
    .hasCustomUi           = oc_runtime::hasCustomUi_factory,
    // The vendor handleButtonEvent reads EVENT_BUTTON_LONG_PRESS on BUTTON_L
    // (long-press clears the grid), so the runtime LONG_RELEASE maps to the
    // vendor LONG_PRESS.
    .customUi              = oc_runtime::dispatch_custom_ui_factory<true>,
    .serialise             = oc_runtime::serialise_factory,
    .deserialise           = oc_runtime::deserialise_factory,
};

}  // namespace

extern "C" __attribute__((visibility("default")))
uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:  return data == 0 ? (uintptr_t)&factory : 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Test seams. The vendor AutomatonnetzState / TransformCell types and the
// `automatonnetz_state` singleton are only fully visible in this TU.
// ---------------------------------------------------------------------------

// The 7 exposed grid settings.
int an_get_setting(_NT_algorithm* /*self*/, int idx) {
    return automatonnetz_state.get_value(static_cast<size_t>(idx));
}
bool an_apply_setting(_NT_algorithm* /*self*/, int idx, int value) {
    return automatonnetz_state.apply_value(static_cast<size_t>(idx), value);
}
int an_setting_count() { return GRID_SETTING_LAST; }
int an_settings_param_base() { return oc_runtime::settings_param_base(); }

// The four rendered tonnetz outputs (root + three triad voices).
void an_get_outputs(_NT_algorithm* /*self*/, int out[4]) {
    automatonnetz_state.tonnetz_state.get_outputs(out);
}

// App-internal grid cells (not NT parameters). grid.Init(cells_) binds the
// walker to this same array, so a cell write reaches both the ISR and Save.
int an_get_cell_setting(int cell, int idx) {
    return automatonnetz_state.cells_[cell].get_value(static_cast<size_t>(idx));
}
void an_set_cell_setting(int cell, int idx, int value) {
    automatonnetz_state.cells_[cell].apply_value(static_cast<size_t>(idx), value);
}

// Queue a grid reset (walker returns to the origin cell on the next ISR tick).
void an_add_reset() {
    automatonnetz_state.AddUserAction(USER_ACTION_RESET);
}

void an_arm_sentinel(_NT_algorithm* self) {
    static_cast<AutomatonnetzInstance*>(self)->alive = true;
}
