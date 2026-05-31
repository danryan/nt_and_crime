// POLYLFO (vendor APP_POLYLFO.h) O_C app port. A single-instance app: the
// vendor owns the file-scope `poly_lfo` SettingsBase singleton, so the settings
// facade points at it directly (no quad facade). Validates the real app through
// the firmware factory lifecycle via the shared plugin_loader, the same path
// the device drives: calculateRequirements -> construct -> step -> draw ->
// customUi -> serialise/deserialise.
//
// This TU does NOT aggregate the OC shim impl. Only plugins/apps/POLYLFO.cpp
// (which defines NT_OC_APP_TU at its top) aggregates; this test links the
// per-app .cpp, which supplies every shim symbol via its single aggregating TU.
//
// POLYLFO output is full-scale modulation: the Frames quadrature engine writes
// four 16-bit DAC codes (lfo.dac_code(0..3)) every isr tick. It is free-running
// (advances every tick with no trigger gate), so the output coverage asserts
// movement and rail-bounds, not gated fire counts. The 10x clock-multiplier
// caveat does not apply (no gated edge-counting in the output path).

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

// Test seams defined in plugins/apps/POLYLFO.cpp. They expose the vendor
// poly_lfo singleton and the runtime view so this TU can mutate and read values
// without pulling the concrete PolyLfo type (and its SETTINGS_DECLARE
// specialization) into its own TU, which would ODR-clash with the aggregating
// .cpp.
int  polylfo_get_setting(_NT_algorithm* self, int idx);
bool polylfo_apply_setting(_NT_algorithm* self, int idx, int value);
int  polylfo_setting_count();
int  polylfo_settings_param_base();
int  polylfo_get_dac_code(int channel);
void polylfo_arm_sentinel(_NT_algorithm* self);

namespace {

// Vendor POLYLFO_SETTINGS enum order (APP_POLYLFO.h:39). VBIAS is #ifdef VOR;
// the shim does not define VOR, so the table ends at TR4_MULT and LAST == 21.
enum {
    PL_COARSE = 0, PL_FINE, PL_TAP_TEMPO, PL_SHAPE, PL_SHAPE_SPREAD, PL_SPREAD,
    PL_COUPLING, PL_ATTENUATION, PL_OFFSET, PL_FREQ_RANGE, PL_FREQ_DIV_B,
    PL_FREQ_DIV_C, PL_FREQ_DIV_D, PL_B_XOR_A, PL_C_XOR_A, PL_D_XOR_A,
    PL_B_AM_BY_A, PL_C_AM_BY_B, PL_D_AM_BY_C, PL_CV4, PL_TR4_MULT, PL_LAST,
};

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

TEST_CASE("POLYLFO loads through the factory path with a custom UI", "[oc_app][polylfo][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);
    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'P', 'L'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->hasCustomUi != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    // The app stores exactly the 21 non-VOR POLYLFO settings.
    REQUIRE(polylfo_setting_count() == PL_LAST);
    REQUIRE(PL_LAST == 21);
}

TEST_CASE("POLYLFO draw renders the menu", "[oc_app][polylfo][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    // The DefaultTitleBar plus the Ch-A frequency line and the settings list
    // draw text, so the screen carries non-zero pixels after a draw.
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("POLYLFO free-running output sweeps all four channels within the rails", "[oc_app][polylfo][isr]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Outputs A..D route to CV out A..D -> default buses 13..16. POLYLFO is
    // free-running: with no trigger and default settings the Frames engine
    // renders every isr tick, so the four codes advance with the LFO phase.
    float* outA = nt::bus_pointer(13, numFrames);
    float* outB = nt::bus_pointer(14, numFrames);
    float* outC = nt::bus_pointer(15, numFrames);
    float* outD = nt::bus_pointer(16, numFrames);

    // Prime one buffer, then sample. Each output must MOVE across the run (the
    // LFO is dynamic, not a static DC) and stay within the +-5V modulation
    // rails (16-bit code space, not railed by a wrong /1536 pitch conversion).
    run_steps(p, numFrames, 1);
    const float a0 = outA[0], b0 = outB[0], c0 = outC[0], d0 = outD[0];
    bool movedA = false, movedB = false, movedC = false, movedD = false;
    for (int s = 0; s < 400; ++s) {
        run_steps(p, numFrames, 1);
        if (outA[0] != Catch::Approx(a0).margin(1e-6)) movedA = true;
        if (outB[0] != Catch::Approx(b0).margin(1e-6)) movedB = true;
        if (outC[0] != Catch::Approx(c0).margin(1e-6)) movedC = true;
        if (outD[0] != Catch::Approx(d0).margin(1e-6)) movedD = true;
        REQUIRE(outA[0] >= -5.1f); REQUIRE(outA[0] <= 5.1f);
        REQUIRE(outB[0] >= -5.1f); REQUIRE(outB[0] <= 5.1f);
        REQUIRE(outC[0] >= -5.1f); REQUIRE(outC[0] <= 5.1f);
        REQUIRE(outD[0] >= -5.1f); REQUIRE(outD[0] <= 5.1f);
    }
    REQUIRE(movedA);
    REQUIRE(movedB);
    REQUIRE(movedC);
    REQUIRE(movedD);

    // The engine codes also vary directly (independent of bus routing): the
    // wavetable lookup produces a non-constant 16-bit code on channel 0.
    const int code0 = polylfo_get_dac_code(0);
    bool code_moved = false;
    for (int s = 0; s < 200 && !code_moved; ++s) {
        run_steps(p, numFrames, 1);
        if (polylfo_get_dac_code(0) != code0) code_moved = true;
    }
    REQUIRE(code_moved);
}

TEST_CASE("POLYLFO settings round-trip through factory serialise/deserialise", "[oc_app][polylfo][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Write distinct in-range values across U8 / I16 / U8-enum settings, enough
    // to prove the 21-setting blob round-trips through Save/Restore.
    REQUIRE(polylfo_apply_setting(p->algorithm, PL_COARSE, 100));     // U8 0..255
    REQUIRE(polylfo_apply_setting(p->algorithm, PL_FINE, -40));       // I16 -128..127
    REQUIRE(polylfo_apply_setting(p->algorithm, PL_SHAPE, 200));      // U8 0..255
    REQUIRE(polylfo_apply_setting(p->algorithm, PL_FREQ_DIV_B, 5));   // U8 enum

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    // Clobber, then restore.
    polylfo_apply_setting(p->algorithm, PL_COARSE, 0);
    polylfo_apply_setting(p->algorithm, PL_FINE, 0);
    polylfo_apply_setting(p->algorithm, PL_SHAPE, 0);
    polylfo_apply_setting(p->algorithm, PL_FREQ_DIV_B, 0);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    REQUIRE(polylfo_get_setting(p->algorithm, PL_COARSE) == 100);
    REQUIRE(polylfo_get_setting(p->algorithm, PL_FINE) == -40);
    REQUIRE(polylfo_get_setting(p->algorithm, PL_SHAPE) == 200);
    REQUIRE(polylfo_get_setting(p->algorithm, PL_FREQ_DIV_B) == 5);
}

TEST_CASE("POLYLFO NT parameter add-on syncs bidirectionally", "[oc_app][polylfo][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = polylfo_settings_param_base();
    polylfo_arm_sentinel(p->algorithm);

    // Direction 1: NT parameter -> app value. Write SHAPE into alg->v and fire
    // the factory parameterChanged; the app setting must follow.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + PL_SHAPE] = 123;
    p->factory->parameterChanged(p->algorithm, base + PL_SHAPE);
    REQUIRE(polylfo_get_setting(p->algorithm, PL_SHAPE) == 123);

    // Direction 2: app-side encoder edit -> NT parameter store. The vendor
    // encoder-L handler edits poly_lfo_state.left_edit_mode (default COARSE) by
    // event.value when tap-tempo is off (the default). Drive one encoder-L turn
    // through the per-app customUi and confirm the NT parameter store mirrors
    // the new COARSE value.
    const int before = polylfo_get_setting(p->algorithm, PL_COARSE);
    _NT_uiData d = oc_ui_sim::make_uidata(0, 0, /*enc_l=*/1, /*enc_r=*/0);
    p->factory->customUi(p->algorithm, d);
    const int after = polylfo_get_setting(p->algorithm, PL_COARSE);
    REQUIRE(after == before + 1);
    REQUIRE(p->algorithm->v[base + PL_COARSE] == after);
}

TEST_CASE("POLYLFO customUI push-back honors the common-parameter offset",
          "[oc_app][polylfo][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    polylfo_arm_sentinel(p->algorithm);

    const int base = polylfo_settings_param_base();
    const int before = polylfo_get_setting(p->algorithm, PL_COARSE);
    _NT_uiData d = oc_ui_sim::make_uidata(0, 0, /*enc_l=*/1, /*enc_r=*/0);
    p->factory->customUi(p->algorithm, d);

    REQUIRE(polylfo_get_setting(p->algorithm, PL_COARSE) == before + 1);
    REQUIRE(p->algorithm->v[base + PL_COARSE] == before + 1);
}
