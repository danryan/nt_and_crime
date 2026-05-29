// FPART (vendor APP_FPART.h) O_C app port. A 4-voice chord-step sequencer with
// a staff-like display. Validates the real app through the same factory
// lifecycle the device firmware drives: calculateRequirements -> construct ->
// step -> draw -> customUi -> serialise/deserialise, loaded via the shared
// plugin_loader factory path.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/FPART.cpp
// (which defines NT_OC_APP_TU at its top) aggregates; this test TU links the
// per-app .cpp, which supplies every shim symbol via its single aggregating TU.
//
// Coverage (per the FPART design spec entry):
//   * large settings-table round-trip: the 406-byte SettingsBase::Save blob
//     (10 head bytes + 99 U32 chord ints) survives serialise/deserialise. This
//     is the kMaxBlobBytes-bump regression: before the bump the blob exceeded
//     the bound and serialise() bailed silently.
//   * pitch output is 1V/oct: a one-step change of a voice's octave setting
//     shifts its routed CV-out by exactly 1.0 V, and a known note at root C /
//     Ionian lands on its computed pitch voltage.
//   * NT-parameter add-on bidirectional sync over the 10 head settings (the
//     only int16-representable settings; the 99 U32 chord ints stay app-internal
//     and are edited through the staff-page customUI, never the param page).

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

// Test seams defined in plugins/apps/FPART.cpp. They expose the vendor
// fpart_instance settings (all 109) and the runtime AppAlgorithm view so this
// TU can mutate and read values without pulling the concrete Fpart type (and
// its SETTINGS_DECLARE specialization) into its own TU, which would ODR-clash
// with the aggregating .cpp.
int  fpart_setting_count();        // FPART_SETTING_LAST (109)
int  fpart_head_param_count();     // settings exposed as NT params (10)
int  fpart_get_setting(_NT_algorithm* self, int idx);
bool fpart_apply_setting(_NT_algorithm* self, int idx, int value);
int  fpart_settings_param_base();
// Chord ints (settings 10..108) are U32 and never NT params: set/read them
// through the vendor build_chord_int / get_chord_int helpers, by chord number
// (0..98) and four note values (0..22 each).
void fpart_set_chord(int chord_num, int a, int b, int c, int d);
int  fpart_get_chord(int chord_num);
// Set the live sequencer position (FPART_SETTING_ACTIVECHORD).
void fpart_set_active_chord(int chord_num);
// Arm the construct-time sentinel (the firmware fires parameterChanged for
// every parameter during construct before the algorithm is registered; the
// runtime guards on a sentinel that flips true after the first draw).
void fpart_arm_sentinel(_NT_algorithm* self);
// Drive an edit of a head setting through the on-device encoder path: switch to
// the parameter page, place the cursor on setting_idx, enter editing, emit
// ENCODER_R, then push back into the NT store, exactly as customUi would.
void fpart_encoder_edit_setting(_NT_algorithm* self, int setting_idx, int delta);

namespace {

// Vendor FPART_SETTINGS enum head (APP_FPART.h:49). Mirrored locally so the
// test can name the head fields without including the vendor header. Chord
// settings (CHORD0..CHORD98) start at index 10 and are not named here.
enum {
    F_ROOT = 0,
    F_SCALE,
    F_LOOPSTART,
    F_LOOPEND,
    F_A_OCTAVE,
    F_B_OCTAVE,
    F_C_OCTAVE,
    F_D_OCTAVE,
    F_LBUTTON_TOGGLE,
    F_ACTIVECHORD,
    F_CHORD0,  // == 10; head count is exactly this
};

constexpr int kSettingLast = 109;

int count_nonzero_screen() {
    int n = 0;
    for (int i = 0; i < 128 * 64; ++i) {
        if (NT_screen[i] != 0) ++n;
    }
    return n;
}

void run_steps(nt::LoadedPlugin* p, int numFrames, int steps) {
    for (int s = 0; s < steps; ++s) {
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
    }
}

}  // namespace

TEST_CASE("FPART loads through the factory path with a custom UI", "[oc_app][fpart][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'F', 'P'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->hasCustomUi != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    // The vendor app declares 109 settings; only the 10 int16-safe head
    // settings are exposed as NT parameters. The 99 U32 chord ints stay
    // app-internal (they exceed int16 range and would corrupt as NT params).
    REQUIRE(fpart_setting_count() == kSettingLast);
    REQUIRE(fpart_head_param_count() == F_CHORD0);
    REQUIRE(fpart_head_param_count() == 10);
}

TEST_CASE("FPART draw renders the staff page", "[oc_app][fpart][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Init() sets the staff page as the default view, which draws the staff
    // lines, the active-chord guide bars, the note symbols, and the side
    // indicators, so the screen carries non-zero pixels after a draw.
    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("FPART large settings table round-trips through serialise/deserialise",
          "[oc_app][fpart][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(fpart_setting_count() == kSettingLast);

    // Head settings: a non-default, in-range value for each of the 10.
    // Ranges (APP_FPART.h SETTINGS_DECLARE): root 0..11, scale 0..6,
    // loopstart 0..97, loopend 1..98, octaves -3..6, long L 0..1,
    // active chord 0..98.
    const int head[10] = {
        /*ROOT*/ 7, /*SCALE*/ 5, /*LOOPSTART*/ 2, /*LOOPEND*/ 40,
        /*A_OCT*/ -2, /*B_OCT*/ 3, /*C_OCT*/ -1, /*D_OCT*/ 6,
        /*LBUTTON*/ 1, /*ACTIVECHORD*/ 25,
    };
    for (int i = 0; i < 10; ++i) {
        REQUIRE(fpart_apply_setting(p->algorithm, i, head[i]));
        REQUIRE(fpart_get_setting(p->algorithm, i) == head[i]);
    }

    // Chord ints: write distinct note patterns across the chord table,
    // including the first, a middle, and the last slot, so the full 406-byte
    // blob carries non-default U32 words end to end.
    struct ChordWrite { int n; int a, b, c, d; };
    const ChordWrite chords[] = {
        {0,  0,  5, 10, 22},
        {1,  22, 0,  3, 17},
        {50, 11, 12, 13, 14},
        {97, 1,  2,  3,  4},
        {98, 20, 19, 18, 7},
    };
    for (const auto& cw : chords) {
        fpart_set_chord(cw.n, cw.a, cw.b, cw.c, cw.d);
    }
    // Spot-check the packing round-trips inside the app before serialising.
    // build_chord_int packs note+10 into 2-digit decimal groups.
    REQUIRE(fpart_get_chord(0) == 10152032);   // (0+10)(5+10)(10+10)(22+10)
    REQUIRE(fpart_get_chord(98) == 30292817);  // (20+10)(19+10)(18+10)(7+10)

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    // The 406-byte blob must actually be emitted. Before the kMaxBlobBytes bump
    // the storage_size() > kMaxBlobBytes guard made serialise() bail and oc_len
    // never appeared, silently dropping persistence.
    REQUIRE(json.find("oc_len") != std::string::npos);

    // Clobber every head setting and every written chord away from its saved
    // value, then deserialise: the saved values must be restored exactly.
    for (int i = 0; i < 10; ++i) {
        fpart_apply_setting(p->algorithm, i, -1000);  // clamps to each field min
        REQUIRE(fpart_get_setting(p->algorithm, i) != head[i]);
    }
    for (const auto& cw : chords) {
        const int written =
            (cw.a + 10) * 1000000 + (cw.b + 10) * 10000 + (cw.c + 10) * 100 + (cw.d + 10);
        fpart_set_chord(cw.n, 9, 9, 9, 9);
        REQUIRE(fpart_get_chord(cw.n) != written);
    }

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int i = 0; i < 10; ++i) {
        REQUIRE(fpart_get_setting(p->algorithm, i) == head[i]);
    }
    for (const auto& cw : chords) {
        const int expect =
            (cw.a + 10) * 1000000 + (cw.b + 10) * 10000 + (cw.c + 10) * 100 + (cw.d + 10);
        REQUIRE(fpart_get_chord(cw.n) == expect);
    }
}

TEST_CASE("FPART isr outputs pitch at 1V/oct on the routed voice buses",
          "[oc_app][fpart][isr]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Root C, Ionian (defaults). Active chord 0, voice A on note 0. With the
    // input buses left silent, the isr's trigger/CV chord-stepping leaves the
    // active chord unchanged, so voice A holds its pitch.
    fpart_apply_setting(p->algorithm, F_ROOT, 0);
    fpart_apply_setting(p->algorithm, F_SCALE, 0);
    fpart_apply_setting(p->algorithm, F_A_OCTAVE, 0);
    fpart_set_chord(0, /*a*/ 0, /*b*/ 0, /*c*/ 0, /*d*/ 0);
    fpart_set_active_chord(0);

    // Voice A routes to CV-out 1 == bus 13 (runtime emit_io_params default).
    float* voiceA = nt::bus_pointer(13, numFrames);

    run_steps(p, numFrames, 4);

    // get_pitch_from_note(0) at root C / Ionian == key_pitches[4] + 3 octaves
    // = 896 + 3*1536 = 5504 pitch units. The runtime converts a DAC code back to
    // volts as pitch/kIntervalSize + octave, i.e. 5504/1536 = 3.5833 V at oct 0.
    const float expect_oct0 = 5504.0f / 1536.0f;
    REQUIRE(voiceA[0] == Catch::Approx(expect_oct0).margin(0.02f));

    // 1V/oct: raise voice A's octave by one and the routed output must rise by
    // exactly 1.0 V. Arm the sentinel so the parameterChanged path applies the
    // octave edit (it is guarded against the construct-time spurious fire).
    fpart_arm_sentinel(p->algorithm);
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    const int base = fpart_settings_param_base();
    v[base + F_A_OCTAVE] = 1;
    p->factory->parameterChanged(p->algorithm, base + F_A_OCTAVE);
    REQUIRE(fpart_get_setting(p->algorithm, F_A_OCTAVE) == 1);

    run_steps(p, numFrames, 4);
    REQUIRE(voiceA[0] == Catch::Approx(expect_oct0 + 1.0f).margin(0.02f));
}

TEST_CASE("FPART NT parameter add-on syncs bidirectionally over the head settings",
          "[oc_app][fpart][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = fpart_settings_param_base();
    fpart_arm_sentinel(p->algorithm);

    // Direction 1: NT parameter -> app value. Write SCALE into alg->v and fire
    // parameterChanged; the app setting must follow.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + F_SCALE] = 4;
    p->factory->parameterChanged(p->algorithm, base + F_SCALE);
    REQUIRE(fpart_get_setting(p->algorithm, F_SCALE) == 4);

    // Direction 2: app-side encoder edit -> NT parameter store. Edit ROOT
    // through the on-device parameter-page encoder path; the push-back must
    // mirror the new value into alg->v via NT_setParameterFromUi.
    const int before = fpart_get_setting(p->algorithm, F_ROOT);
    fpart_encoder_edit_setting(p->algorithm, F_ROOT, +3);
    const int after = fpart_get_setting(p->algorithm, F_ROOT);
    REQUIRE(after == before + 3);
    REQUIRE(p->algorithm->v[base + F_ROOT] == after);
}

TEST_CASE("FPART customUI push-back honors the common-parameter offset",
          "[oc_app][fpart][param-sync][offset]") {
    // The device firmware injects common parameters ahead of the plug-in table,
    // so plug-in parameter P lives at global index P + NT_parameterOffset().
    // NT_setParameterFromUi takes the GLOBAL index, so the push-back must add
    // NT_parameterOffset(). Model a +1 prefix and edit one head setting through
    // the on-device encoder path; the edit must land on that setting only.
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = fpart_settings_param_base();
    fpart_arm_sentinel(p->algorithm);

    const int target = F_LOOPEND;
    const int neighbor = F_LOOPSTART;  // the setting one row above target
    const int target_before = fpart_get_setting(p->algorithm, target);
    const int neighbor_before = fpart_get_setting(p->algorithm, neighbor);

    fpart_encoder_edit_setting(p->algorithm, target, +5);

    REQUIRE(fpart_get_setting(p->algorithm, target) == target_before + 5);
    REQUIRE(p->algorithm->v[base + target] == target_before + 5);
    // The neighbor must be untouched: an offset-less push-back would have
    // re-applied the edit one global index low, landing on this row.
    REQUIRE(fpart_get_setting(p->algorithm, neighbor) == neighbor_before);
    REQUIRE(p->algorithm->v[base + neighbor] == neighbor_before);

    nt::set_parameter_offset(0);
}
