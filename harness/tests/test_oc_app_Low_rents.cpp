// Low-rents (vendor APP_LORENZ.h) O_C app port. Validates the real app through
// the same factory lifecycle the device firmware drives: calculateRequirements
// -> construct -> step -> draw -> customUi -> serialise/deserialise, loaded via
// the shared plugin_loader factory path.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/Low_rents.cpp
// (which defines NT_OC_APP_TU at its top) aggregates; this test TU links the
// per-app .cpp, which supplies every shim symbol via its single aggregating TU.
//
// Coverage (per the spec Low-rents entry):
//   * settings round-trip over the 10 LORENZ_SETTING_* fields;
//   * isr produces DAC output on the routed CV-out buses for a known
//     frequency/rho injected on the routed CV-in buses;
//   * the reset trigger fires exactly once per rising edge under the tick
//     accumulator (one edge re-inits the generator, not once per inner isr tick);
//   * NT-parameter add-on bidirectional sync over the 10 settings.

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

// Test seams defined in plugins/apps/Low_rents.cpp. They expose the embedded
// LorenzGenerator settings instance and the runtime AppAlgorithm view so this
// TU can mutate and read values without pulling the concrete LorenzGenerator
// type (and its SETTINGS_DECLARE specialization) into its own TU, which would
// ODR-clash with the aggregating .cpp.
int  low_rents_get_setting(_NT_algorithm* self, int idx);
bool low_rents_apply_setting(_NT_algorithm* self, int idx, int value);
int  low_rents_setting_count();
int  low_rents_settings_param_base();
// Drive the vendor app's encoder edit on the FREQ1 setting (selected generator
// 0) so the test can observe the app-side push back into the NT parameter store.
void low_rents_encoder_edit_freq1(_NT_algorithm* self, int delta);
// Drive an edit of an arbitrary list setting through the on-device encoder path
// (cursor + ENCODER_R), so the test can observe the push-back targeting.
void low_rents_encoder_edit_setting(_NT_algorithm* self, int setting_idx, int delta);
// Arm the construct-time sentinel (the firmware fires parameterChanged during
// construct before the algorithm is registered; the runtime guards on a
// sentinel that the first draw() flips true).
void low_rents_arm_sentinel(_NT_algorithm* self);

namespace {

// Vendor LORENZ_SETTINGS enum order (APP_LORENZ.h:33). Mirrored locally so the
// test can name fields without including the vendor header.
enum {
    L_FREQ1 = 0,
    L_FREQ2,
    L_RHO1,
    L_RHO2,
    L_FREQ_RANGE1,
    L_FREQ_RANGE2,
    L_OUT_A,
    L_OUT_B,
    L_OUT_C,
    L_OUT_D,
    L_LAST,
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

TEST_CASE("Low-rents loads through the factory path with a custom UI", "[oc_app][low_rents][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'L', 'R'));
    REQUIRE(p->factory->construct != nullptr);
    REQUIRE(p->factory->step != nullptr);
    REQUIRE(p->factory->draw != nullptr);
    REQUIRE(p->factory->hasCustomUi != nullptr);
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    // The app stores exactly the 10 LORENZ settings.
    REQUIRE(low_rents_setting_count() == L_LAST);
    REQUIRE(L_LAST == 10);
}

TEST_CASE("Low-rents draw renders the Lorenz menu", "[oc_app][low_rents][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);  // O_C apps own the whole screen
    // The DualTitleBar plus the settings list draw text, so the screen carries
    // non-zero pixels after a draw.
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Low-rents settings round-trip through factory serialise/deserialise", "[oc_app][low_rents][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(low_rents_setting_count() == 10);

    // Mutate every one of the 10 settings to a non-default, in-range value.
    // Ranges (APP_LORENZ.h:153): freq 0..255, rho 4..127, range 0..4,
    // out 0..LORENZ_OUTPUT_LAST-1 (21).
    const int written[L_LAST] = {
        /*FREQ1*/ 200, /*FREQ2*/ 50, /*RHO1*/ 100, /*RHO2*/ 7,
        /*FREQ_RANGE1*/ 4, /*FREQ_RANGE2*/ 1,
        /*OUT_A*/ 5, /*OUT_B*/ 6, /*OUT_C*/ 7, /*OUT_D*/ 8,
    };
    for (int i = 0; i < L_LAST; ++i) {
        REQUIRE(low_rents_apply_setting(p->algorithm, i, written[i]));
        REQUIRE(low_rents_get_setting(p->algorithm, i) == written[i]);
    }

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    // Clobber every setting away from its saved value, then deserialise: the
    // saved values must be restored exactly. apply_value clamps into range and
    // returns false when the clamped value equals the current one, so the
    // clobber writes a very low value that every field clamps to its own min
    // (every written[] value above is strictly above its field's min, so the
    // min is always distinct) rather than asserting the return.
    for (int i = 0; i < L_LAST; ++i) {
        low_rents_apply_setting(p->algorithm, i, -1000);
        REQUIRE(low_rents_get_setting(p->algorithm, i) != written[i]);
    }

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int i = 0; i < L_LAST; ++i) {
        REQUIRE(low_rents_get_setting(p->algorithm, i) == written[i]);
    }
}

TEST_CASE("Low-rents isr produces DAC output for a known frequency and rho", "[oc_app][low_rents][isr]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Defaults: OUT_A=X1, OUT_B=Y1, OUT_C=X2, OUT_D=Y2. With a nonzero freq the
    // Lorenz state advances and dac_code() returns nonzero values on every
    // routed output. Routing defaults: CV-out A..D -> buses 13..16, CV-in
    // 1..4 -> buses 1..4 (freq1, rho1, freq2, rho2 per the vendor isr order).
    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Inject a moderate positive frequency CV on the freq inputs (channels 1
    // and 3) and a moderate rho CV on channels 2 and 4. Values are in hem
    // units; the isr scales them (cv * 16) and adds the settings base. A
    // positive offset keeps the generator running.
    float* freq1_bus = nt::bus_pointer(1, numFrames);
    float* rho1_bus  = nt::bus_pointer(2, numFrames);
    float* freq2_bus = nt::bus_pointer(3, numFrames);
    float* rho2_bus  = nt::bus_pointer(4, numFrames);
    for (int i = 0; i < numFrames; ++i) {
        freq1_bus[i] = 1.0f;
        rho1_bus[i]  = 0.5f;
        freq2_bus[i] = 1.0f;
        rho2_bus[i]  = 0.5f;
    }

    float* outA = nt::bus_pointer(13, numFrames);

    // Run enough buffers for the smoother (kSmoothing=16) to settle and the
    // Lorenz integrator to advance well away from its initial point.
    run_steps(p, numFrames, 200);

    // The X1 output (default OUT_A) is biased by +32769 in dac_code, so it is
    // strongly nonzero on the output bus regardless of integration detail.
    REQUIRE(outA[0] != Catch::Approx(0.0f).margin(1e-6));

    // The bias above makes "nonzero" pass even if the generator never advanced.
    // Strengthen the assertion: sample the output across several more buffers
    // and require it to CHANGE. A chaotic Lorenz integrator driven by a nonzero
    // frequency must keep moving; a static biased constant (route-and-flush
    // plumbing that wrote the seed once) would not. This proves the generator
    // actually responds, not just that the route wrote a biased constant.
    const float sample0 = outA[0];
    bool output_changed = false;
    for (int s = 0; s < 50 && !output_changed; ++s) {
        run_steps(p, numFrames, 1);
        if (outA[0] != Catch::Approx(sample0).margin(1e-6)) output_changed = true;
    }
    REQUIRE(output_changed);
}

TEST_CASE("Low-rents reset trigger fires exactly once per rising edge", "[oc_app][low_rents][edges]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 48;
    nt::set_bus_frame_count(numFrames);

    // Drive a frequency so the integrator moves between edges, and route a
    // reset on TR-in 1 (DIGITAL_INPUT_1 -> reset1, default bus 5). The vendor
    // isr calls Init(0) on a reset1 edge, which snaps Lx1_ back to its seed.
    float* freq1_bus = nt::bus_pointer(1, numFrames);
    for (int i = 0; i < numFrames; ++i) freq1_bus[i] = 2.0f;

    float* reset_bus = nt::bus_pointer(5, numFrames);
    float* outA      = nt::bus_pointer(13, numFrames);

    // Advance the integrator with the reset low.
    for (int i = 0; i < numFrames; ++i) reset_bus[i] = 0.0f;
    run_steps(p, numFrames, 60);
    const float advanced = outA[0];

    // Raise the reset high and hold it across many buffers. The one-edge-per-
    // tick discipline means the reset is seen by exactly one isr tick (the
    // first low->high), so the generator re-inits once, not once per tick. The
    // post-reset output snaps toward the seed (Init sets Lx1_ to its start),
    // distinct from the advanced state.
    for (int i = 0; i < numFrames; ++i) reset_bus[i] = 5.0f;
    p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
    const float just_after_reset = outA[0];

    // The reset must have re-initialized the generator: the output right after
    // the reset edge differs from the long-advanced state.
    REQUIRE(just_after_reset != Catch::Approx(advanced).margin(1e-6));

    // Held high across more buffers: no further reset re-fires (the edge stays
    // latched), so the integrator advances normally away from the seed again.
    // If the reset re-fired every tick, the output would be pinned at the seed
    // value and never move. Drive enough to guarantee movement.
    run_steps(p, numFrames, 60);
    REQUIRE(outA[0] != Catch::Approx(just_after_reset).margin(1e-6));
}

TEST_CASE("Low-rents NT parameter add-on syncs bidirectionally", "[oc_app][low_rents][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = low_rents_settings_param_base();
    REQUIRE(base == low_rents_settings_param_base());

    // Arm the construct-time sentinel (the firmware fires parameterChanged for
    // every parameter during construct before the algorithm is registered; the
    // runtime guards on a sentinel that flips true after the first draw).
    low_rents_arm_sentinel(p->algorithm);

    // --- Direction 1: NT parameter -> app value. ---
    // The firmware writes the new value into the algorithm's canonical
    // parameter array (alg->v) and then fires parameterChanged. Mirror that:
    // write RHO1 into alg->v (cast away const exactly as the firmware/harness
    // do) and fire the factory parameterChanged; the app setting must follow.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + L_RHO1] = 99;
    p->factory->parameterChanged(p->algorithm, base + L_RHO1);
    REQUIRE(low_rents_get_setting(p->algorithm, L_RHO1) == 99);

    // --- Direction 2: app-side encoder edit -> NT parameter store. ---
    // The vendor app edits FREQ1 directly through its encoder handler. The
    // per-app customUi push-back must mirror the new value into the NT
    // parameter store (alg->v) via NT_setParameterFromUi.
    const int before = low_rents_get_setting(p->algorithm, L_FREQ1);
    low_rents_encoder_edit_freq1(p->algorithm, +10);
    const int after = low_rents_get_setting(p->algorithm, L_FREQ1);
    REQUIRE(after == before + 10);

    // The NT parameter store (alg->v) must reflect the app-side edit.
    REQUIRE(p->algorithm->v[base + L_FREQ1] == after);
}

TEST_CASE("Low-rents customUI push-back honors the common-parameter offset",
          "[oc_app][low_rents][param-sync][offset]") {
    // The device firmware injects common parameters (Bypass) ahead of the
    // plug-in table, so plug-in parameter P lives at global index
    // P + NT_parameterOffset(). NT_setParameterFromUi takes the GLOBAL index, so
    // the customUI push-back must add NT_parameterOffset(). Model a +1 prefix and
    // edit one list setting through the on-device encoder path; the edit must
    // land on that setting only. A push-back that forgets the offset writes one
    // global index low and the firmware re-applies it to the setting above the
    // edited one, which is the off-by-one observed on hardware.
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    low_rents_arm_sentinel(p->algorithm);

    const int base = low_rents_settings_param_base();
    const int rho1_before = low_rents_get_setting(p->algorithm, L_RHO1);
    const int rho2_before = low_rents_get_setting(p->algorithm, L_RHO2);

    // Edit RHO2 by +5 via the cursor/ENCODER_R path.
    low_rents_encoder_edit_setting(p->algorithm, L_RHO2, +5);

    // RHO2 took the edit; RHO1 (the neighbor above) must be untouched.
    REQUIRE(low_rents_get_setting(p->algorithm, L_RHO2) == rho2_before + 5);
    REQUIRE(low_rents_get_setting(p->algorithm, L_RHO1) == rho1_before);

    // The store slot for RHO2 (plugin-relative) must carry the new value.
    REQUIRE(p->algorithm->v[base + L_RHO2] == rho2_before + 5);
}
