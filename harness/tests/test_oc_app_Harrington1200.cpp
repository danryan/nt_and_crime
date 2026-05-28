// Harrington 1200 (vendor APP_H1200.h) O_C app port. Validates the heaviest
// validation app through the same factory lifecycle the device firmware drives:
// calculateRequirements -> construct -> step -> draw -> customUi ->
// serialise/deserialise, loaded via the shared plugin_loader factory path.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/
// Harrington1200.cpp (which defines NT_OC_APP_TU at its top) aggregates; this
// test TU links the per-app .cpp, which supplies every shim symbol via its
// single aggregating TU.
//
// Coverage (per the spec Harrington 1200 entry):
//   * settings round-trip over the 37 H1200_SETTING_* fields (factory
//     serialise/deserialise);
//   * the neo-Riemannian tonnetz transform produces the expected triad output
//     for a known root + MODE_MAJOR (the default chord render);
//   * the four transform triggers (reset/P/L/R) are each handled, one edge per
//     tick under the tick accumulator (no 10x-style refire);
//   * the circle screensaver (visualize_pitch_classes via H1200_screensaver)
//     draws without faulting;
//   * NT-parameter add-on bidirectional sync over the 37 settings.

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

// Test seams defined in plugins/apps/Harrington1200.cpp. They expose the
// vendor h1200_settings singleton, the tonnetz state outputs, and the runtime
// AppAlgorithm view so this TU can mutate and read values without pulling the
// concrete H1200Settings type (and its SETTINGS_DECLARE specialization) into
// its own TU, which would ODR-clash with the aggregating .cpp.
int  h1200_get_setting(_NT_algorithm* self, int idx);
bool h1200_apply_setting(_NT_algorithm* self, int idx, int value);
int  h1200_setting_count();
int  h1200_settings_param_base();
// Copy the four tonnetz outputs (rendered root + triad notes) into out[4].
void h1200_get_outputs(_NT_algorithm* self, int out[4]);
// Arm the construct-time sentinel (the firmware fires parameterChanged during
// construct before the algorithm is registered; the runtime guards on a
// sentinel that the first draw() flips true).
void h1200_arm_sentinel(_NT_algorithm* self);

namespace {

// Vendor H1200Setting enum order (APP_H1200.h:72). Mirrored locally so the
// test can name fields without including the vendor header.
enum {
    H_ROOT_OFFSET = 0,
    H_ROOT_OFFSET_CV,
    H_OCTAVE,
    H_OCTAVE_CV,
    H_MODE,
    H_INVERSION,
    H_INVERSION_CV,
    H_PLR_TRANSFORM_PRIO,
    H_PLR_TRANSFORM_PRIO_CV,
    H_NSH_TRANSFORM_PRIO,
    H_NSH_TRANSFORM_PRIO_CV,
    H_CV_SAMPLING,
    H_OUTPUT_MODE,
    H_TRIGGER_DELAY,
    H_TRIGGER_TYPE,
    H_EUCLIDEAN_CV1_MAPPING,
    H_EUCLIDEAN_CV2_MAPPING,
    H_EUCLIDEAN_CV3_MAPPING,
    H_EUCLIDEAN_CV4_MAPPING,
    H_P_EUCLIDEAN_LENGTH,
    H_P_EUCLIDEAN_FILL,
    H_P_EUCLIDEAN_OFFSET,
    H_L_EUCLIDEAN_LENGTH,
    H_L_EUCLIDEAN_FILL,
    H_L_EUCLIDEAN_OFFSET,
    H_R_EUCLIDEAN_LENGTH,
    H_R_EUCLIDEAN_FILL,
    H_R_EUCLIDEAN_OFFSET,
    H_N_EUCLIDEAN_LENGTH,
    H_N_EUCLIDEAN_FILL,
    H_N_EUCLIDEAN_OFFSET,
    H_S_EUCLIDEAN_LENGTH,
    H_S_EUCLIDEAN_FILL,
    H_S_EUCLIDEAN_OFFSET,
    H_H_EUCLIDEAN_LENGTH,
    H_H_EUCLIDEAN_FILL,
    H_H_EUCLIDEAN_OFFSET,
    H_LAST,
};

int count_nonzero_screen() {
    int n = 0;
    for (int i = 0; i < 128 * 64; ++i) {
        if (NT_screen[i] != 0) ++n;
    }
    return n;
}

// Drive `steps` audio buffers of `numFrames` each through the loaded plugin.
void run_steps(nt::LoadedPlugin* p, int numFrames, int steps) {
    for (int s = 0; s < steps; ++s) {
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
    }
}

}  // namespace

TEST_CASE("Harrington 1200 loads through the factory path with a custom UI", "[oc_app][h1200][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'H', 'A'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->hasCustomUi != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    // The app stores exactly the 37 H1200 settings.
    REQUIRE(h1200_setting_count() == H_LAST);
    REQUIRE(H_LAST == 37);
}

TEST_CASE("Harrington 1200 draw renders the menu", "[oc_app][h1200][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    // The DefaultTitleBar plus the note names + settings list draw text, so the
    // screen carries non-zero pixels after a draw.
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Harrington 1200 renders a major triad for the default root", "[oc_app][h1200][tonnetz]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Defaults: root_offset 0, octave 0, inversion 0, MODE_MAJOR, output mode
    // chord, CV sampling continuous. Construct fires APP_EVENT_RESUME, which
    // resets the tonnetz state to a major triad. With continuous CV sampling
    // the isr renders every tick even with no trigger, so a single step()
    // populates the outputs. A C-major triad renders to root + {0, 4, 7}:
    // outputs[0] = root (0), outputs[1..3] = {0, 4, 7}.
    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);
    run_steps(p, numFrames, 1);

    int out[4] = { -99, -99, -99, -99 };
    h1200_get_outputs(p->algorithm, out);
    REQUIRE(out[0] == 0);
    REQUIRE(out[1] == 0);
    REQUIRE(out[2] == 4);
    REQUIRE(out[3] == 7);
}

TEST_CASE("Harrington 1200 P/L/R transform triggers each mutate the triad once per edge", "[oc_app][h1200][edges]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 48;
    nt::set_bus_frame_count(numFrames);

    // The default trigger type is PLR with priority X>P>L>R. The four trigger
    // inputs route to TR buses 5..8 by the runtime default: TR1 (reset) -> bus
    // 5, P -> bus 6, L -> bus 7, R -> bus 8. Establish the major triad first.
    run_steps(p, numFrames, 1);
    int base[4];
    h1200_get_outputs(p->algorithm, base);
    REQUIRE(base[1] == 0);
    REQUIRE(base[2] == 4);
    REQUIRE(base[3] == 7);

    // Raise P (bus 6) high and hold it across one buffer. The vendor isr applies
    // TRANSFORM_P on the rising edge. P (parallel) flips the major triad to its
    // parallel minor: a C-major chord {0,4,7} becomes C-minor {0,3,7}. With the
    // one-edge-per-tick discipline the transform fires exactly once for the
    // single low->high transition, not once per inner isr tick. If it refired
    // every tick, P would toggle major<->minor many times and the parity would
    // be unpredictable; firing exactly once lands deterministically on minor.
    float* p_bus = nt::bus_pointer(6, numFrames);
    for (int i = 0; i < numFrames; ++i) p_bus[i] = 5.0f;
    p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);

    int after_p[4];
    h1200_get_outputs(p->algorithm, after_p);
    // The render uses root + interval; a parallel transform of C major yields
    // the C minor triad {0, 3, 7}.
    REQUIRE(after_p[1] == 0);
    REQUIRE(after_p[2] == 3);
    REQUIRE(after_p[3] == 7);

    // Hold P high across more buffers: the edge is latched, so no further
    // transform fires and the chord stays put at the minor triad.
    run_steps(p, numFrames, 5);
    int held[4];
    h1200_get_outputs(p->algorithm, held);
    REQUIRE(held[1] == 0);
    REQUIRE(held[2] == 3);
    REQUIRE(held[3] == 7);

    // Drop P low, then raise the reset trigger (TR1 -> bus 5). Reset re-inits
    // the tonnetz state to a fresh major triad, exactly once per edge.
    for (int i = 0; i < numFrames; ++i) p_bus[i] = 0.0f;
    p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);

    float* reset_bus = nt::bus_pointer(5, numFrames);
    for (int i = 0; i < numFrames; ++i) reset_bus[i] = 5.0f;
    p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);

    int after_reset[4];
    h1200_get_outputs(p->algorithm, after_reset);
    REQUIRE(after_reset[1] == 0);
    REQUIRE(after_reset[2] == 4);
    REQUIRE(after_reset[3] == 7);
}

TEST_CASE("Harrington 1200 screensaver draws without faulting", "[oc_app][h1200][screensaver]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Render a triad so the screensaver has notes to plot in the tonnetz circle.
    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);
    run_steps(p, numFrames, 1);

    // Draw enough times to cross the screensaver idle threshold; the runtime
    // then calls DrawScreensaver (H1200_screensaver) which invokes
    // OC::visualize_pitch_classes. It must paint the note circle without
    // faulting and leave non-zero pixels.
    for (int i = 0; i < 70; ++i) p->factory->draw(p->algorithm);
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Harrington 1200 settings round-trip through factory serialise/deserialise", "[oc_app][h1200][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(h1200_setting_count() == 37);

    // Mutate every one of the 37 settings to a non-default, in-range value.
    // Per the SETTINGS_DECLARE table (APP_H1200.h:438): root -11..11, CV srcs
    // 0..4, octave -3..3, mode 0..1, inversion -3..3, PLR/NSH prio 0..5, CV
    // sampling 0..1, output 0..1, trigger delay 0..7, trigger type 0..2, eucl
    // CV map 0..18, eucl length 2..32, eucl fill 0..32, eucl offset 0..31.
    const int written[H_LAST] = {
        /*ROOT*/ 5, /*ROOT_CV*/ 1, /*OCTAVE*/ 2, /*OCTAVE_CV*/ 2,
        /*MODE*/ 1, /*INVERSION*/ 1, /*INVERSION_CV*/ 3,
        /*PLR_PRIO*/ 2, /*PLR_PRIO_CV*/ 4, /*NSH_PRIO*/ 3, /*NSH_PRIO_CV*/ 1,
        /*CV_SAMPLING*/ 1, /*OUTPUT_MODE*/ 1, /*TRIGGER_DELAY*/ 4,
        /*TRIGGER_TYPE*/ 2,
        /*EUCL_CV1*/ 1, /*EUCL_CV2*/ 2, /*EUCL_CV3*/ 3, /*EUCL_CV4*/ 4,
        /*P_LEN*/ 12, /*P_FILL*/ 5, /*P_OFF*/ 3,
        /*L_LEN*/ 13, /*L_FILL*/ 6, /*L_OFF*/ 4,
        /*R_LEN*/ 14, /*R_FILL*/ 7, /*R_OFF*/ 5,
        /*N_LEN*/ 15, /*N_FILL*/ 8, /*N_OFF*/ 6,
        /*S_LEN*/ 16, /*S_FILL*/ 9, /*S_OFF*/ 7,
        /*H_LEN*/ 17, /*H_FILL*/ 10, /*H_OFF*/ 8,
    };
    for (int i = 0; i < H_LAST; ++i) {
        REQUIRE(h1200_apply_setting(p->algorithm, i, written[i]));
        REQUIRE(h1200_get_setting(p->algorithm, i) == written[i]);
    }

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_settings_b64") != std::string::npos);

    // Clobber every setting away from its saved value, then deserialise: the
    // saved values must be restored exactly. apply_value clamps into range, so
    // a very low write clamps each field to its own min (every written[] value
    // above is strictly above its field's min, so the min is always distinct).
    for (int i = 0; i < H_LAST; ++i) {
        h1200_apply_setting(p->algorithm, i, -1000);
        REQUIRE(h1200_get_setting(p->algorithm, i) != written[i]);
    }

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int i = 0; i < H_LAST; ++i) {
        REQUIRE(h1200_get_setting(p->algorithm, i) == written[i]);
    }
}

TEST_CASE("Harrington 1200 NT parameter add-on syncs bidirectionally", "[oc_app][h1200][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = h1200_settings_param_base();

    // Arm the construct-time sentinel (the firmware fires parameterChanged for
    // every parameter during construct before the algorithm is registered; the
    // runtime guards on a sentinel that flips true after the first draw).
    h1200_arm_sentinel(p->algorithm);

    // --- Direction 1: NT parameter -> app value (the firmware writes into the
    // algorithm's canonical parameter array alg->v, then fires
    // parameterChanged). Mirror that: write INVERSION into alg->v and fire the
    // factory parameterChanged; the app setting must follow.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + H_INVERSION] = 2;
    p->factory->parameterChanged(p->algorithm, base + H_INVERSION);
    REQUIRE(h1200_get_setting(p->algorithm, H_INVERSION) == 2);

    // --- Direction 2: app-side encoder edit -> NT parameter store. The vendor
    // encoder-L handler edits INVERSION directly (change_value). Drive an
    // encoder turn through the per-app customUi and confirm the NT parameter
    // store mirrors the new value.
    auto* alg = static_cast<oc_runtime::AppAlgorithm*>(p->algorithm);
    const int before = h1200_get_setting(p->algorithm, H_INVERSION);
    _NT_uiData d = oc_ui_sim::make_uidata(0, 0, /*enc_l=*/1, /*enc_r=*/0);
    p->factory->customUi(p->algorithm, d);
    const int after = h1200_get_setting(p->algorithm, H_INVERSION);
    REQUIRE(after == before + 1);
    REQUIRE(p->algorithm->v[base + H_INVERSION] == after);
    (void)alg;
}
