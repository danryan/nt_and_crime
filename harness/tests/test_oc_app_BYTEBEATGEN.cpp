// BYTEBEATGEN (vendor APP_BYTEBEATGEN.h) O_C app port. Quad-channel app: the app
// object is QuadByteBeats (4 ByteBeat SettingsBase instances of 19 settings each),
// so the settings facade is a quad facade dispatching 76 logical rows across the
// four channels (idx/19 = channel, idx%19 = setting). The first app to exceed the
// runtime kMaxSettings=64 cap (raised to 80) and the first with a conditional
// (enabled-settings) vendor menu. Validates the real app through the firmware
// factory lifecycle via the shared plugin_loader.
//
// Only plugins/apps/BYTEBEATGEN.cpp (NT_OC_APP_TU) aggregates the OC shim impl;
// this TU links it and reaches the embedded state through the test seams below.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "oc_ui_sim.h"
#include <distingnt/api.h>

#include "OC_apps.h"
#include "OC_core.h"

#include <cstring>
#include <cmath>
#include <string>

// Test seams defined in plugins/apps/BYTEBEATGEN.cpp.
int  bytebeatgen_get_setting(int channel, int setting);
bool bytebeatgen_apply_setting(int channel, int setting, int value);
int  bytebeatgen_setting_count();          // 4 * BYTEBEAT_SETTING_LAST == 76
int  bytebeatgen_settings_per_channel();   // BYTEBEAT_SETTING_LAST == 19
int  bytebeatgen_settings_param_base();
const char* bytebeatgen_param_name(int idx);  // channel-prefixed NT row name
int  bytebeatgen_enabled_count(int channel);  // num_enabled_settings after refresh
void bytebeatgen_arm_sentinel(_NT_algorithm* self);

namespace {
enum {
    BYTEBEAT_EQUATION = 0, BYTEBEAT_SPEED, BYTEBEAT_PITCH, BYTEBEAT_P0,
    BYTEBEAT_P1, BYTEBEAT_P2, BYTEBEAT_LOOP_MODE, BYTEBEAT_LOOP_START,
    BYTEBEAT_LOOP_START_MED, BYTEBEAT_LOOP_START_FINE, BYTEBEAT_LOOP_END,
    BYTEBEAT_LOOP_END_MED, BYTEBEAT_LOOP_END_FINE, BYTEBEAT_TRIGGER_INPUT,
    BYTEBEAT_STEP_MODE, BYTEBEAT_CV1, BYTEBEAT_CV2, BYTEBEAT_CV3, BYTEBEAT_CV4,
    BYTEBEAT_LAST,
};
void run_steps(nt::LoadedPlugin* p, int numFrames, int steps) {
    for (int s = 0; s < steps; ++s)
        p->factory->step(p->algorithm, nt::bus_frames_base(), numFrames / 4);
}
}  // namespace

TEST_CASE("BYTEBEATGEN loads through the factory path with a custom UI", "[oc_app][bytebeatgen][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);
    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'B', 'T'));
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->deserialise != nullptr);

    REQUIRE(bytebeatgen_settings_per_channel() == BYTEBEAT_LAST);
    REQUIRE(BYTEBEAT_LAST == 19);
    REQUIRE(bytebeatgen_setting_count() == 76);
}

TEST_CASE("BYTEBEATGEN exposes 88 parameters with channel-prefixed setting names", "[oc_app][bytebeatgen][params]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // 12 I/O routing rows + 76 settings == 88.
    const int base = bytebeatgen_settings_param_base();
    REQUIRE(base == 12);
    REQUIRE(p->algorithm->parameters[base].name != nullptr);
    // The channel-prefixed name override must reach the actual NT parameter
    // table. First and last settings rows.
    REQUIRE(std::string(p->algorithm->parameters[base].name) == "A Equation");
    REQUIRE(std::string(p->algorithm->parameters[base + 75].name) == "D CV4 -> ");
    // The standalone builder seam agrees (channel A/B EQUATION, channel D last).
    REQUIRE(std::string(bytebeatgen_param_name(0)) == "A Equation");
    REQUIRE(std::string(bytebeatgen_param_name(BYTEBEAT_LAST)) == "B Equation");
    REQUIRE(std::string(bytebeatgen_param_name(75)) == "D CV4 -> ");
    // Every name fits the 16-byte buffer (2 prefix + <=13 + null).
    for (int i = 0; i < 76; ++i)
        REQUIRE(std::strlen(bytebeatgen_param_name(i)) <= 15);
}

TEST_CASE("BYTEBEATGEN parameter rows route to the correct channel and setting", "[oc_app][bytebeatgen][routing]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    bytebeatgen_arm_sentinel(p->algorithm);
    const int base = bytebeatgen_settings_param_base();

    // Direction NT -> app: writing global row (base + ch*19 + setting) then
    // firing parameterChanged updates ONLY bytebeats_[ch] setting, no other channel.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    const int ch = 2, setting = BYTEBEAT_EQUATION;  // "C Equation"
    const int row = ch * BYTEBEAT_LAST + setting;
    const int other_before = bytebeatgen_get_setting(0, BYTEBEAT_EQUATION);
    v[base + row] = 9;  // in-range for EQUATION (0..15)
    p->factory->parameterChanged(p->algorithm, base + row);
    REQUIRE(bytebeatgen_get_setting(ch, setting) == 9);
    REQUIRE(bytebeatgen_get_setting(0, BYTEBEAT_EQUATION) == other_before);  // channel A untouched
}

TEST_CASE("BYTEBEATGEN settings round-trip per channel and gap-free", "[oc_app][bytebeatgen][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Write distinct in-range values into EQUATION (0..15) and SPEED (0..255) for
    // every channel; read back equal, and prove per-channel isolation.
    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bytebeatgen_apply_setting(ch, BYTEBEAT_EQUATION, ch + 1));
        REQUIRE(bytebeatgen_apply_setting(ch, BYTEBEAT_SPEED, 200 - ch * 10));
    }
    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bytebeatgen_get_setting(ch, BYTEBEAT_EQUATION) == ch + 1);
        REQUIRE(bytebeatgen_get_setting(ch, BYTEBEAT_SPEED) == 200 - ch * 10);
    }
}

TEST_CASE("BYTEBEATGEN free-running output is full-scale within +-5V", "[oc_app][bytebeatgen][isr]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Channel A -> CV out A -> default bus 13. Bytebeat is free-running (step_mode
    // off by default), so the output advances every sample without a trigger. The
    // sample reaches the DAC as value = zero_offset(32768) + (int16_t)b, routed by
    // the 16-bit code model to +-5V full scale. The default equation ("hope")
    // occupies the positive half of that span; what matters for the regression
    // guard is that the output is FULL-SCALE modulation, not the pitch /1536 path.
    float* outA = nt::bus_pointer(13, numFrames);

    run_steps(p, numFrames, 40);

    // Sample over a long window; assert it stays in the +-5V rails, genuinely
    // moves, AND reaches a substantial amplitude (> 1V). The amplitude floor is
    // the specific guard: a reintroduced /1536 pitch conversion would rail or
    // collapse the signal, and a mere "moved within range" check would pass for a
    // near-zero signal.
    const float first = outA[0];
    bool moved = false;
    float peak_abs = 0.0f;
    for (int s = 0; s < 400; ++s) {
        run_steps(p, numFrames, 1);
        const float v = outA[0];
        REQUIRE(v >= -5.1f);
        REQUIRE(v <= 5.1f);
        if (v != Catch::Approx(first).margin(1e-6)) moved = true;
        if (std::fabs(v) > peak_abs) peak_abs = std::fabs(v);
    }
    REQUIRE(moved);
    REQUIRE(peak_abs > 1.0f);
}

TEST_CASE("BYTEBEATGEN conditional menu toggles enabled-settings count with loop mode", "[oc_app][bytebeatgen][menu]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // update_enabled_settings hides the six loop settings when LOOP_MODE is off:
    // 7 head + 6 tail = 13 enabled; with loop on, +6 loop rows = 19. LOOP_MODE
    // defaults to 0, so the off case is the construct state (apply_value is a
    // no-op then and returns false; assert the count, not the apply return).
    REQUIRE(bytebeatgen_enabled_count(0) == 13);
    REQUIRE(bytebeatgen_apply_setting(0, BYTEBEAT_LOOP_MODE, 1));
    REQUIRE(bytebeatgen_enabled_count(0) == 19);
    REQUIRE(bytebeatgen_apply_setting(0, BYTEBEAT_LOOP_MODE, 0));
    REQUIRE(bytebeatgen_enabled_count(0) == 13);
}

TEST_CASE("BYTEBEATGEN settings survive factory serialise/deserialise", "[oc_app][bytebeatgen][persist]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bytebeatgen_apply_setting(ch, BYTEBEAT_EQUATION, ch + 2));
        REQUIRE(bytebeatgen_apply_setting(ch, BYTEBEAT_P0, 100 + ch * 5));
    }

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (int ch = 0; ch < 4; ++ch) {
        bytebeatgen_apply_setting(ch, BYTEBEAT_EQUATION, 0);
        bytebeatgen_apply_setting(ch, BYTEBEAT_P0, 0);
    }
    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int ch = 0; ch < 4; ++ch) {
        REQUIRE(bytebeatgen_get_setting(ch, BYTEBEAT_EQUATION) == ch + 2);
        REQUIRE(bytebeatgen_get_setting(ch, BYTEBEAT_P0) == 100 + ch * 5);
    }
}
