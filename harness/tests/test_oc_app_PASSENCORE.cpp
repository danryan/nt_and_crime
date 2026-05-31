// Passencore (vendor APP_PASSENCORE.h) O_C app port. Validates the first
// mid-array setting-subset app: 17 of 18 settings are NT parameters, with the
// U16 scale mask (PASSENCORE_SETTING_MASK, index 1) excluded because it
// overflows int16 and is edited through the scale-editor customUI. The facade
// remaps logical row i to physical setting `i >= 1 ? i + 1 : i`; the mask still
// persists through the full-settings Save/Restore blob.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/PASSENCORE.cpp
// (which defines NT_OC_APP_TU at its top) aggregates; this test TU links the
// per-app .cpp, which supplies every shim symbol via its single aggregating TU.
//
// Coverage (per the spec PASSENCORE entry):
//   * factory lifecycle + registration (GUID OCPS, exactly 17 exposed settings);
//   * the subset remap: logical row 1 addresses SAMPLE_TRIGGER, not MASK;
//   * the menu draws without faulting;
//   * chord output: a sample trigger computes and plays a chord on the four DAC
//     voices (defaults route SAMPLE and TARGET to the same input, so one edge
//     both samples and plays);
//   * settings round-trip over the 17 exposed rows AND the excluded mask;
//   * NT-parameter add-on bidirectional sync, including the common-parameter
//     offset push-back.

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

// Test seams defined in plugins/apps/PASSENCORE.cpp. The setting seams take a
// LOGICAL row index (exposed-parameter numbering) and remap to the physical
// setting, so a test addresses the 17 exposed rows the way the NT table does.
int  pc_get_setting(_NT_algorithm* self, int logical);
bool pc_apply_setting(_NT_algorithm* self, int logical, int value);
int  pc_setting_count();
int  pc_settings_param_base();
int  pc_get_mask();
void pc_set_mask(int value);
void pc_get_outputs(int out[4]);
void pc_arm_sentinel(_NT_algorithm* self);

namespace {

// Logical exposed-row numbering (physical PASSENCORE_SETTINGS minus MASK).
enum {
    L_SCALE = 0,
    L_SAMPLE_TRIGGER,
    L_TARGET_TRIGGER,
    L_PASSING_TRIGGER,
    L_RESET_TRIGGER,
    L_A_OCTAVE,
    L_A_MIDRANGE,
    L_B_OCTAVE,
    L_B_MIDRANGE,
    L_C_OCTAVE,
    L_C_MIDRANGE,
    L_D_OCTAVE,
    L_D_MIDRANGE,
    L_BORROW_CHORDS,
    L_BASE_COLOR,
    L_CV3_ROLE,
    L_CV4_ROLE,
    L_LAST,
};

// Runtime I/O routing param layout: 4 CV in, 4 CV out, 4 trig in.
constexpr int kTrigInBase = 4 + 4;  // v[8 + i] selects trigger input i's bus.

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

TEST_CASE("Passencore loads through the factory path with a custom UI", "[oc_app][passencore][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'P', 'S'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->hasCustomUi != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    // Subset: exactly 17 settings are NT parameters (MASK excluded).
    REQUIRE(pc_setting_count() == L_LAST);
    REQUIRE(L_LAST == 17);
}

TEST_CASE("Passencore exposes a contiguous subset skipping the U16 mask", "[oc_app][passencore][subset]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Logical row 0 maps to SCALE (physical 0); logical row 1 maps to
    // SAMPLE_TRIGGER (physical 2), NOT the mask. The trigger settings are 0..3;
    // a write that round-trips on logical row 1 proves the remap reaches
    // SAMPLE_TRIGGER and never touches the U16 mask at physical index 1.
    REQUIRE(pc_apply_setting(p->algorithm, L_SAMPLE_TRIGGER, 3));
    REQUIRE(pc_get_setting(p->algorithm, L_SAMPLE_TRIGGER) == 3);

    // The mask is addressable only off the parameter table; it keeps its U16
    // default and is unaffected by the logical-row writes.
    REQUIRE(pc_get_mask() == 65535);
}

TEST_CASE("Passencore draw renders the menu", "[oc_app][passencore][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Passencore plays a chord on a sample trigger", "[oc_app][passencore][chord]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Route trigger input 1 (which carries the default SAMPLE and TARGET roles,
    // both setting value 0 -> DIGITAL_INPUT_1) to bus 1, and clear it.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[kTrigInBase + 0] = 1;
    float* trig_bus = nt::bus_pointer(1, numFrames);
    for (int i = 0; i < numFrames; ++i) trig_bus[i] = 0.0f;
    run_steps(p, numFrames, 1);  // establish the low baseline (no edge latched)

    // Raise the trigger bus and step: the ISR sees one rising edge on input 1,
    // so SAMPLE computes a new target chord and TARGET plays it via set_pitch.
    for (int i = 0; i < numFrames; ++i) trig_bus[i] = 5.0f;
    run_steps(p, numFrames, 1);

    int out[4] = { 0, 0, 0, 0 };
    pc_get_outputs(out);

    // A chord is a multi-voice spread, not silence or a unison: at least two of
    // the four voices carry distinct pitch codes.
    const bool multi_voice =
        out[0] != out[1] || out[0] != out[2] || out[0] != out[3];
    REQUIRE(multi_voice);
}

TEST_CASE("Passencore settings round-trip through factory serialise/deserialise", "[oc_app][passencore][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(pc_setting_count() == 17);

    // In-range, non-default values per exposed row. Enum numerics: COLORS
    // POWER..JAZZ = 0..4; CV_ROLE NONE..BASS = 0..3; SCALE_SEMI = 5.
    const int written[L_LAST] = {
        /*SCALE*/ 8,
        /*SAMPLE*/ 1, /*TARGET*/ 1, /*PASSING*/ 1, /*RESET*/ 0,
        /*A_OCT*/ 2, /*A_MID*/ 3, /*B_OCT*/ 0, /*B_MID*/ -3,
        /*C_OCT*/ 1, /*C_MID*/ 2, /*D_OCT*/ 0, /*D_MID*/ 4,
        /*BORROW*/ 0, /*COLOR*/ 2, /*CV3*/ 2, /*CV4*/ 3,
    };
    // apply_value returns false on a no-op write (SettingsBase reports "no
    // change"), so assert the post-state, not the apply return (BYTEBEATGEN
    // lesson). The values above are all in range, so each row holds what was
    // written regardless of whether it differed from the prior value.
    for (int i = 0; i < L_LAST; ++i) {
        pc_apply_setting(p->algorithm, i, written[i]);
        REQUIRE(pc_get_setting(p->algorithm, i) == written[i]);
    }

    // The excluded mask is set to a non-default 12-bit value to prove it
    // persists through the blob despite never being a parameter.
    pc_set_mask(0x0AAA);
    REQUIRE(pc_get_mask() == 0x0AAA);

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    // Clobber every exposed row and the mask, then restore from the blob.
    for (int i = 0; i < L_LAST; ++i) pc_apply_setting(p->algorithm, i, -1000);
    pc_set_mask(1);
    REQUIRE(pc_get_mask() == 1);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int i = 0; i < L_LAST; ++i)
        REQUIRE(pc_get_setting(p->algorithm, i) == written[i]);
    REQUIRE(pc_get_mask() == 0x0AAA);
}

TEST_CASE("Passencore NT parameter add-on syncs bidirectionally", "[oc_app][passencore][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = pc_settings_param_base();
    pc_arm_sentinel(p->algorithm);

    // Direction 1: NT parameter -> app value. Write A_OCTAVE (logical 5, range
    // -2..3) into alg->v and fire parameterChanged; the app setting must follow.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + L_A_OCTAVE] = 2;
    p->factory->parameterChanged(p->algorithm, base + L_A_OCTAVE);
    REQUIRE(pc_get_setting(p->algorithm, L_A_OCTAVE) == 2);

    // Direction 2: app-side encoder edit -> NT parameter store. Events reach the
    // vendor handlers only through the factory customUi (dispatch_custom_ui).
    // The menu cursor starts at SCALE (logical 0, physical 0, != MASK), not
    // editing; a short BUTTON_R press/release toggles editing on, then ENCODER_R
    // changes the cursor's setting (SCALE). The runtime push-back mirrors it.
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(kNT_encoderButtonR, 0, 0, 0));
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, kNT_encoderButtonR, 0, 0));
    const int before = pc_get_setting(p->algorithm, L_SCALE);
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));
    const int after = pc_get_setting(p->algorithm, L_SCALE);
    REQUIRE(after == before + 1);
    REQUIRE(p->algorithm->v[base + L_SCALE] == after);
}

TEST_CASE("Passencore customUI push-back honors the common-parameter offset",
          "[oc_app][passencore][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    pc_arm_sentinel(p->algorithm);

    const int base = pc_settings_param_base();
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(kNT_encoderButtonR, 0, 0, 0));
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, kNT_encoderButtonR, 0, 0));
    const int before = pc_get_setting(p->algorithm, L_SCALE);
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));

    REQUIRE(pc_get_setting(p->algorithm, L_SCALE) == before + 1);
    REQUIRE(p->algorithm->v[base + L_SCALE] == before + 1);
}
