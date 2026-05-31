// Automatonnetz (vendor APP_AUTOMATONNETZ.h) O_C app port. Validates the first
// grid-subset app: a single-instance SettingsBase whose 7 global GRID settings
// are exposed as NT parameters while its 25-cell transform grid (100 settings)
// stays app-internal, edited through the vendor 2D grid customUI and persisted
// whole through the Save/Restore blob.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/
// AUTOMATONNETZ.cpp (which defines NT_OC_APP_TU at its top) aggregates; this
// test TU links the per-app .cpp, which supplies every shim symbol via its
// single aggregating TU.
//
// Coverage (per the spec AUTOMATONNETZ entry):
//   * factory lifecycle + registration (GUID OCAN, exactly 7 grid settings);
//   * the menu draws without faulting;
//   * pitch output: forcing the origin cell to TRANSFORM_NONE and resetting
//     renders the tonnetz init major triad (same engine as Harrington 1200);
//   * grid-setting round-trip over the 7 GRID_SETTING_* fields;
//   * a mutated grid cell survives the blob though it is not an NT parameter;
//   * NT-parameter add-on bidirectional sync over the grid settings, including
//     the common-parameter offset push-back.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "oc_ui_sim.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"

#include <cstring>
#include <cstdint>
#include <string>

// Test seams defined in plugins/apps/AUTOMATONNETZ.cpp.
int  an_get_setting(_NT_algorithm* self, int idx);
bool an_apply_setting(_NT_algorithm* self, int idx, int value);
int  an_setting_count();
int  an_settings_param_base();
void an_get_outputs(_NT_algorithm* self, int out[4]);
int  an_get_cell_setting(int cell, int idx);
void an_set_cell_setting(int cell, int idx, int value);
void an_add_reset();
void an_arm_sentinel(_NT_algorithm* self);

namespace {

// Vendor GridSettings enum order (APP_AUTOMATONNETZ.h:158).
enum {
    G_DX = 0,
    G_DY,
    G_MODE,
    G_OCTAVE,
    G_TRIGGER_DELAY,
    G_OUTPUTMODE,
    G_CLEARMODE,
    G_LAST,
};

// Vendor CellSettings enum order (APP_AUTOMATONNETZ.h:88).
enum {
    C_TRANSFORM = 0,
    C_TRANSPOSE,
    C_INVERSION,
    C_EVENT,
};

constexpr int TRANSFORM_NONE = 0;  // tonnetz::TRANSFORM_NONE (first enumerator).

int count_nonzero_screen() {
    int n = 0;
    for (int i = 0; i < 128 * 64; ++i)
        if (NT_screen[i] != 0) ++n;
    return n;
}

void run_steps(nt::LoadedPlugin* p, int numFrames, int steps) {
    for (int s = 0; s < steps; ++s)
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
}

}  // namespace

TEST_CASE("Automatonnetz loads through the factory path with a custom UI", "[oc_app][automatonnetz][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'A', 'N'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->hasCustomUi != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    // Grid subset: exactly the 7 global GRID settings are NT parameters; the
    // 25-cell transform grid stays app-internal.
    REQUIRE(an_setting_count() == G_LAST);
    REQUIRE(G_LAST == 7);
}

TEST_CASE("Automatonnetz draw renders the menu", "[oc_app][automatonnetz][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Automatonnetz renders the tonnetz major triad at the origin cell", "[oc_app][automatonnetz][tonnetz]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Construct's ClearGrid(CLEAR_MODE_RAND_TRANSFORM) randomizes every cell.
    // Force the origin cell (grid index 0, where MoveToOrigin lands) to a no-op
    // transform with zero transpose/inversion, then queue a reset and step. The
    // walker lands on the origin cell, applies TRANSFORM_NONE (the tonnetz state
    // stays at its init major triad), and renders. Same tonnetz engine as
    // Harrington 1200, whose major triad renders to root + {0, 4, 7}.
    an_set_cell_setting(0, C_TRANSFORM, TRANSFORM_NONE);
    an_set_cell_setting(0, C_TRANSPOSE, 0);
    an_set_cell_setting(0, C_INVERSION, 0);
    an_add_reset();

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);
    run_steps(p, numFrames, 1);

    int out[4] = { -99, -99, -99, -99 };
    an_get_outputs(p->algorithm, out);
    REQUIRE(out[0] == 0);
    REQUIRE(out[1] == 0);
    REQUIRE(out[2] == 4);
    REQUIRE(out[3] == 7);
}

TEST_CASE("Automatonnetz grid settings round-trip through factory serialise/deserialise", "[oc_app][automatonnetz][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(an_setting_count() == 7);

    // Mutate every grid setting to a non-default, in-range value. Ranges from
    // SETTINGS_DECLARE (APP_AUTOMATONNETZ.h:351): dx/dy 0..39, mode 0..(LAST-1),
    // octave -3..3, trigger delay 0..kNumDelayTimes-1, output 0.., clear 0...
    const int written[G_LAST] = {
        /*DX*/ 12, /*DY*/ 9, /*MODE*/ 1, /*OCTAVE*/ 2,
        /*TRIGGER_DELAY*/ 3, /*OUTPUTMODE*/ 1, /*CLEARMODE*/ 1,
    };
    for (int i = 0; i < G_LAST; ++i) {
        REQUIRE(an_apply_setting(p->algorithm, i, written[i]));
        REQUIRE(an_get_setting(p->algorithm, i) == written[i]);
    }

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (int i = 0; i < G_LAST; ++i) {
        an_apply_setting(p->algorithm, i, -1000);
        REQUIRE(an_get_setting(p->algorithm, i) != written[i]);
    }

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int i = 0; i < G_LAST; ++i)
        REQUIRE(an_get_setting(p->algorithm, i) == written[i]);
}

TEST_CASE("Automatonnetz grid cells persist through the blob though they are not NT parameters", "[oc_app][automatonnetz][cells]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // A cell is not an NT parameter (grid subset), but Automatonnetz_save packs
    // all 25 cells after the grid settings, so a mutated cell must survive the
    // serialise/deserialise round-trip. Mutate cell 5's transform and transpose.
    an_set_cell_setting(5, C_TRANSFORM, 2);
    an_set_cell_setting(5, C_TRANSPOSE, 7);
    REQUIRE(an_get_cell_setting(5, C_TRANSFORM) == 2);
    REQUIRE(an_get_cell_setting(5, C_TRANSPOSE) == 7);

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();

    // Clobber the cell, then deserialise: the saved cell values must return.
    an_set_cell_setting(5, C_TRANSFORM, 0);
    an_set_cell_setting(5, C_TRANSPOSE, 0);
    REQUIRE(an_get_cell_setting(5, C_TRANSFORM) == 0);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    REQUIRE(an_get_cell_setting(5, C_TRANSFORM) == 2);
    REQUIRE(an_get_cell_setting(5, C_TRANSPOSE) == 7);
}

TEST_CASE("Automatonnetz NT parameter add-on syncs bidirectionally", "[oc_app][automatonnetz][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = an_settings_param_base();
    an_arm_sentinel(p->algorithm);

    // Direction 1: NT parameter -> app value. Write MODE into alg->v and fire
    // parameterChanged; the app setting must follow.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + G_MODE] = 1;
    p->factory->parameterChanged(p->algorithm, base + G_MODE);
    REQUIRE(an_get_setting(p->algorithm, G_MODE) == 1);

    // Direction 2: app-side encoder edit -> NT parameter store. Events reach the
    // vendor handlers only through the factory customUi (dispatch_custom_ui);
    // the runtime customUi alone just does bookkeeping. The grid cursor starts
    // at GRID_SETTING_DX and is not editing, so a short BUTTON_R press/release
    // (emitted on the release edge) toggles grid editing on, then ENCODER_R
    // changes the cursor's setting (DX). The runtime push-back must mirror it.
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(kNT_encoderButtonR, 0, 0, 0));
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, kNT_encoderButtonR, 0, 0));
    const int before = an_get_setting(p->algorithm, G_DX);
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));
    const int after = an_get_setting(p->algorithm, G_DX);
    REQUIRE(after == before + 1);
    REQUIRE(p->algorithm->v[base + G_DX] == after);
}

TEST_CASE("Automatonnetz customUI push-back honors the common-parameter offset",
          "[oc_app][automatonnetz][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    an_arm_sentinel(p->algorithm);

    const int base = an_settings_param_base();
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(kNT_encoderButtonR, 0, 0, 0));
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, kNT_encoderButtonR, 0, 0));
    const int before = an_get_setting(p->algorithm, G_DX);
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));

    REQUIRE(an_get_setting(p->algorithm, G_DX) == before + 1);
    REQUIRE(p->algorithm->v[base + G_DX] == before + 1);
}
