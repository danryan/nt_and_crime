// BBGEN (vendor APP_BBGEN.h) O_C app port. First quad-channel app: the app
// object is QuadBouncingBalls (4 BouncingBall SettingsBase instances), so the
// settings facade is a quad facade dispatching 44 logical rows across the four
// balls (idx/11 = channel, idx%11 = setting). Validates the real app through the
// firmware factory lifecycle via the shared plugin_loader.
//
// Only plugins/apps/BBGEN.cpp (NT_OC_APP_TU) aggregates the OC shim impl; this
// TU links it and reaches the embedded state through the test seams below.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "oc_ui_sim.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_core.h"

#include <cstring>
#include <string>

// Test seams defined in plugins/apps/BBGEN.cpp.
int  bbgen_get_setting(int channel, int setting);
bool bbgen_apply_setting(int channel, int setting, int value);
int  bbgen_setting_count();          // 4 * BB_SETTING_LAST == 44
int  bbgen_settings_per_channel();   // BB_SETTING_LAST == 11
int  bbgen_settings_param_base();
const char* bbgen_param_name(int idx);  // channel-prefixed NT row name
void bbgen_arm_sentinel(_NT_algorithm* self);

namespace {
enum {
    BB_GRAVITY = 0, BB_BOUNCE_LOSS, BB_INITIAL_AMPLITUDE, BB_INITIAL_VELOCITY,
    BB_TRIGGER_INPUT, BB_RETRIGGER_BOUNCES, BB_CV1, BB_CV2, BB_CV3, BB_CV4,
    BB_HARD_RESET, BB_LAST,
};
void run_steps(nt::LoadedPlugin* p, int numFrames, int steps) {
    for (int s = 0; s < steps; ++s)
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
}
}  // namespace

TEST_CASE("BBGEN loads through the factory path with a custom UI", "[oc_app][bbgen][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);
    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'B', 'B'));
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);

    REQUIRE(bbgen_settings_per_channel() == BB_LAST);
    REQUIRE(BB_LAST == 11);
    REQUIRE(bbgen_setting_count() == 44);
}

TEST_CASE("BBGEN exposes 56 parameters with channel-prefixed setting names", "[oc_app][bbgen][params]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // 12 I/O routing rows + 44 settings.
    REQUIRE(p->algorithm->parameters[bbgen_settings_param_base()].name != nullptr);
    // Row 0 of the settings block is channel A, setting GRAVITY -> "A Gravity".
    REQUIRE(std::string(bbgen_param_name(0)) == "A Gravity");
    // Channel B, GRAVITY -> "B Gravity".
    REQUIRE(std::string(bbgen_param_name(BB_LAST)) == "B Gravity");
    // Channel D, HARD_RESET -> "D Hard reset" (last row).
    REQUIRE(std::string(bbgen_param_name(43)) == "D Hard reset");
}

TEST_CASE("BBGEN parameter rows route to the correct ball and setting", "[oc_app][bbgen][routing]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    bbgen_arm_sentinel(p->algorithm);
    const int base = bbgen_settings_param_base();

    // Direction NT -> app: writing global row (base + ch*11 + setting) then
    // firing parameterChanged updates ONLY balls_[ch] setting, no other channel.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    const int ch = 2, setting = BB_GRAVITY;  // "C Gravity"
    const int row = ch * BB_LAST + setting;
    const int other_before = bbgen_get_setting(0, BB_GRAVITY);
    v[base + row] = 77;
    p->factory->parameterChanged(p->algorithm, base + row);
    REQUIRE(bbgen_get_setting(ch, setting) == 77);
    REQUIRE(bbgen_get_setting(0, BB_GRAVITY) == other_before);  // channel A untouched
}

TEST_CASE("BBGEN gate-triggered envelope outputs within 0V..+5V and moves", "[oc_app][bbgen][isr]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Ball A's default trigger input is DIGITAL_INPUT_1 -> TR in 1 -> default
    // bus 5. Raise it high to gate ball A; the unipolar envelope rises then
    // decays. Ball A -> CV out A -> default bus 13.
    float* trig1 = nt::bus_pointer(5, numFrames);
    float* outA  = nt::bus_pointer(13, numFrames);
    for (int i = 0; i < numFrames; ++i) trig1[i] = 5.0f;

    run_steps(p, numFrames, 40);

    // Output must move (envelope is dynamic, not a static constant) and stay
    // within the unipolar 0V..+5V rails (modulation code space, not railed
    // full-scale by a wrong /1536 conversion).
    const float sample0 = outA[0];
    bool moved = false;
    for (int s = 0; s < 80 && !moved; ++s) {
        run_steps(p, numFrames, 1);
        if (outA[0] != Catch::Approx(sample0).margin(1e-6)) moved = true;
    }
    REQUIRE(moved);
    for (int s = 0; s < 80; ++s) {
        run_steps(p, numFrames, 1);
        REQUIRE(outA[0] >= -0.1f);
        REQUIRE(outA[0] <= 5.1f);
    }
}

TEST_CASE("BBGEN settings round-trip through factory serialise/deserialise", "[oc_app][bbgen][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Write a distinct in-range value into every channel's GRAVITY and
    // BOUNCE_LOSS (U8 0..255), enough to prove the 4-ball blob round-trips.
    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bbgen_apply_setting(ch, BB_GRAVITY, 50 + ch * 10));
        REQUIRE(bbgen_apply_setting(ch, BB_BOUNCE_LOSS, 200 - ch * 10));
    }

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    // Clobber, then restore.
    for (int ch = 0; ch < 4; ++ch) {
        bbgen_apply_setting(ch, BB_GRAVITY, 0);
        bbgen_apply_setting(ch, BB_BOUNCE_LOSS, 0);
    }
    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bbgen_get_setting(ch, BB_GRAVITY) == 50 + ch * 10);
        REQUIRE(bbgen_get_setting(ch, BB_BOUNCE_LOSS) == 200 - ch * 10);
    }
}
