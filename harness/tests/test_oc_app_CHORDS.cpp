// Chords (vendor APP_CHORDS.h) O_C app port. A four-voice chord sequencer that is
// a mid-array setting-subset app like PASSENCORE: 29 of 30 settings are NT
// parameters, with the U16 scale mask (CHORDS_SETTING_MASK, index 3) excluded
// because it overflows int16 and is edited through the scale-editor customUI. The
// facade remaps logical row i to physical setting `i >= 3 ? i + 1 : i`; the mask
// still persists through the full-settings Save/Restore blob.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/CHORDS.cpp
// (which defines NT_OC_APP_TU at its top) aggregates; this test TU links the
// per-app .cpp, which supplies every shim symbol via its single aggregating TU.
//
// Coverage:
//   * factory lifecycle + registration (GUID OCCH, exactly 29 exposed settings);
//   * the subset remap: logical rows 0..2 address SCALE/ROOT/PROGRESSION, logical
//     row 3 skips the U16 mask and addresses CV_SOURCE; no exposed row hits MASK;
//   * the menu draws without faulting;
//   * the four DAC voices are driven when a trigger advances the progression;
//   * settings round-trip over the 29 exposed rows AND the excluded mask;
//   * NT-parameter add-on bidirectional sync, including the common-parameter
//     offset push-back. The cursor starts on MASK (BUTTON_R there opens the scale
//     editor), so the encoder-edit direction scrolls to ROOT first.

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

// Test seams defined in plugins/apps/CHORDS.cpp. The setting seams take a LOGICAL
// row index (exposed-parameter numbering) and remap to the physical setting, so a
// test addresses the 29 exposed rows the way the NT table does.
int  ch_get_setting(_NT_algorithm* self, int logical);
bool ch_apply_setting(_NT_algorithm* self, int logical, int value);
int  ch_setting_count();
int  ch_settings_total();
int  ch_settings_param_base();
int  ch_phys_setting(int logical);
int  ch_mask_index();
int  ch_get_mask();
void ch_set_mask(int value);
void ch_get_outputs(int out[4]);
void ch_arm_sentinel(_NT_algorithm* self);

namespace {

// Physical CHORDS_SETTINGS indices used in the test (APP_CHORDS.h:44).
enum {
    P_SCALE = 0, P_ROOT, P_PROGRESSION, P_MASK, P_CV_SOURCE,
    P_ADV_TRIG, P_PLAYMODES, P_DIRECTION, P_BROWNIAN, P_TRIG_DELAY,
    P_TRANSPOSE, P_OCTAVE, P_CHORD_SLOT,
};

// Logical exposed rows: physical with MASK (index 3) removed. Rows < 3 are
// identity; row 3 onward shift up by one physical slot.
enum {
    L_SCALE = 0, L_ROOT, L_PROGRESSION, L_CV_SOURCE, L_ADV_TRIG,
    L_PLAYMODES, L_DIRECTION,
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

TEST_CASE("Chords loads through the factory path with a custom UI", "[oc_app][chords][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'C', 'H'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    // Subset: exactly 30 settings are NT parameters (MASK excluded).
    REQUIRE(ch_settings_total() == 31);
    REQUIRE(ch_setting_count() == 30);
    REQUIRE(ch_mask_index() == P_MASK);
}

TEST_CASE("Chords exposes a contiguous subset skipping the U16 mask", "[oc_app][chords][subset]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Rows 0..2 map straight through; the mask (physical 3) is skipped, so
    // logical row 3 lands on physical CV_SOURCE (4).
    REQUIRE(ch_phys_setting(0) == P_SCALE);
    REQUIRE(ch_phys_setting(1) == P_ROOT);
    REQUIRE(ch_phys_setting(2) == P_PROGRESSION);
    REQUIRE(ch_phys_setting(3) == P_CV_SOURCE);   // mask skipped

    // No exposed row maps onto the mask slot.
    for (int w = 0; w < ch_setting_count(); ++w)
        REQUIRE(ch_phys_setting(w) != P_MASK);

    // The mask keeps its U16 default and is unaffected by logical-row writes.
    REQUIRE(ch_get_mask() == 65535);
}

TEST_CASE("Chords draw renders the menu", "[oc_app][chords][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Chords drives its DAC voices when a trigger advances", "[oc_app][chords][chord]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // The chord advances on a trigger (default advance source is TR2). Route all
    // four trigger inputs to buses 5..8 and feed a CV; pulse the triggers so the
    // selected advance input sees a rising edge and CHORDS::Update voices the
    // chord onto the four DAC channels. A played pitch (even 0V = code 32768) is
    // non-zero, so a chord write leaves at least one output non-zero.
    //
    // This asserts the path runs end to end (ISR -> trigger -> chord -> DAC), not
    // the exact pitch: the vendor main output routes through a calibrated DAC the
    // shim collapses to an uncalibrated 1V/oct model, so the precise code is not
    // host-meaningful. Pitch fidelity is a hardware-smoke concern.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    for (int tr = 0; tr < 4; ++tr) v[kTrigInBase + tr] = 5 + tr;  // trig in -> bus 5..8
    v[0] = 1;  // CV in 1 -> bus 1
    float* cv_bus = nt::bus_pointer(1, numFrames);
    for (int i = 0; i < numFrames; ++i) cv_bus[i] = 1.0f;

    run_steps(p, numFrames, 1);  // low baseline
    for (int tr = 0; tr < 4; ++tr) {
        float* trig = nt::bus_pointer(5 + tr, numFrames);
        for (int i = 0; i < numFrames; ++i) trig[i] = 5.0f;
    }
    run_steps(p, numFrames, 4);

    int out[4]; ch_get_outputs(out);
    const bool any_output = out[0] || out[1] || out[2] || out[3];
    REQUIRE(any_output);
}

TEST_CASE("Chords settings and mask round-trip through serialise/deserialise", "[oc_app][chords][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(ch_setting_count() == 30);

    // Mutate a representative set of exposed rows. apply_value returns false on a
    // no-op write, so assert the post-state, not the apply return (BYTEBEATGEN
    // lesson). All values below are in range.
    struct W { int logical, value; };
    const W writes[] = {
        {L_SCALE, 8}, {L_ROOT, 5}, {L_PROGRESSION, 2},
        {L_CV_SOURCE, 1}, {L_ADV_TRIG, 1}, {L_PLAYMODES, 3},
    };
    for (const W& w : writes) {
        ch_apply_setting(p->algorithm, w.logical, w.value);
        REQUIRE(ch_get_setting(p->algorithm, w.logical) == w.value);
    }

    // The excluded mask is set to a non-default 12-bit value to prove it persists
    // through the blob despite never being a parameter.
    ch_set_mask(0x0AAA);
    REQUIRE(ch_get_mask() == 0x0AAA);

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (const W& w : writes) ch_apply_setting(p->algorithm, w.logical, 0);
    ch_set_mask(1);
    REQUIRE(ch_get_mask() == 1);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (const W& w : writes)
        REQUIRE(ch_get_setting(p->algorithm, w.logical) == w.value);
    REQUIRE(ch_get_mask() == 0x0AAA);
}

TEST_CASE("Chords NT parameter add-on syncs bidirectionally", "[oc_app][chords][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = ch_settings_param_base();
    ch_arm_sentinel(p->algorithm);

    // Direction 1: NT parameter -> app value. Write ROOT (logical 1, range 0..11)
    // into alg->v and fire parameterChanged; the app setting must follow.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + L_ROOT] = 4;
    p->factory->parameterChanged(p->algorithm, base + L_ROOT);
    REQUIRE(ch_get_setting(p->algorithm, L_ROOT) == 4);

    // Direction 2: app-side encoder edit -> NT parameter store. Events reach the
    // vendor handlers only through the factory customUi (dispatch_custom_ui). The
    // cursor starts on MASK (cursor position 0), where BUTTON_R opens the scale
    // editor; scroll the cursor one step (ENCODER_R while not editing) to land on
    // ROOT, toggle editing with a BUTTON_R press/release, then ENCODER_R edits
    // ROOT. The runtime push-back mirrors it into v[base + L_ROOT].
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));  // scroll to ROOT
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(kNT_encoderButtonR, 0, 0, 0));
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, kNT_encoderButtonR, 0, 0));
    const int before = ch_get_setting(p->algorithm, L_ROOT);
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));
    const int after = ch_get_setting(p->algorithm, L_ROOT);
    REQUIRE(after == before + 1);
    REQUIRE(p->algorithm->v[base + L_ROOT] == after);
}

TEST_CASE("Chords customUI push-back honors the common-parameter offset",
          "[oc_app][chords][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    ch_arm_sentinel(p->algorithm);

    const int base = ch_settings_param_base();
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));  // scroll to ROOT
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(kNT_encoderButtonR, 0, 0, 0));
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, kNT_encoderButtonR, 0, 0));
    const int before = ch_get_setting(p->algorithm, L_ROOT);
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));

    REQUIRE(ch_get_setting(p->algorithm, L_ROOT) == before + 1);
    REQUIRE(p->algorithm->v[base + L_ROOT] == before + 1);
}
