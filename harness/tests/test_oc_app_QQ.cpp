// Quantermain (vendor APP_QQ.h) O_C app port. Validates the quad-quantizer at
// N = 4 with a single per-channel U16 mask excluded: four QuantizerChannel
// instances of 51 settings each, of which CHANNEL_SETTING_MASK (index 2) is
// excluded per channel, leaving 50 exposed rows per channel (200 flat NT rows).
// The facade is the BBGEN/DQ quad-facade shape with N = 4 plus a within-channel
// remap that skips the mask; the masks persist through the whole-app
// QQ_save/QQ_restore blob.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/QQ.cpp
// aggregates; this test TU links the per-app .cpp.

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

// Test seams defined in plugins/apps/QQ.cpp. Setting seams take a
// (channel, physical-setting) pair so a test can address any setting, including
// the excluded mask.
int  qq_get_setting(int channel, int setting);
bool qq_apply_setting(int channel, int setting, int value);
int  qq_num_channels();
int  qq_settings_per_channel_total();
int  qq_exposed_per_channel();
int  qq_setting_count();
int  qq_settings_param_base();
int  qq_phys_in_channel(int within);
int  qq_get_mask(int channel);
void qq_set_mask(int channel, int value);
void qq_get_outputs(int out[4]);
void qq_arm_sentinel(_NT_algorithm* self);

namespace {

// Vendor ChannelSetting physical indices (APP_QQ.h:65).
enum {
    P_SCALE = 0, P_ROOT, P_MASK, P_SOURCE, P_AUX_SOURCE_DEST, P_TRIGGER,
    P_CLKDIV, P_DELAY, P_TRANSPOSE, P_OCTAVE, P_FINE,
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

// Flat NT row -> (channel, within); within -> physical via qq_phys_in_channel.
int flat_to_phys(int flat) { return qq_phys_in_channel(flat % qq_exposed_per_channel()); }
int flat_channel(int flat) { return flat / qq_exposed_per_channel(); }

}  // namespace

TEST_CASE("Quantermain loads through the factory path with a custom UI", "[oc_app][qq][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'Q', 'Q'));
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    REQUIRE(qq_num_channels() == 4);
    REQUIRE(qq_exposed_per_channel() == qq_settings_per_channel_total() - 1);  // mask excluded
    REQUIRE(qq_setting_count() == 4 * (qq_settings_per_channel_total() - 1));  // 200
}

TEST_CASE("Quantermain exposes a per-channel subset skipping the U16 mask", "[oc_app][qq][subset]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Rows 0..1 map straight through; the mask (physical 2) is skipped, so
    // within-row 2 lands on physical SOURCE (3).
    REQUIRE(qq_phys_in_channel(0) == P_SCALE);
    REQUIRE(qq_phys_in_channel(1) == P_ROOT);
    REQUIRE(qq_phys_in_channel(2) == P_SOURCE);   // mask skipped
    REQUIRE(qq_phys_in_channel(3) == P_AUX_SOURCE_DEST);

    // No exposed row maps onto the mask slot.
    const int per = qq_exposed_per_channel();
    for (int w = 0; w < per; ++w)
        REQUIRE(qq_phys_in_channel(w) != P_MASK);
}

TEST_CASE("Quantermain draw renders the menu", "[oc_app][qq][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Quantermain drives its DAC outputs when stepped", "[oc_app][qq][quantize]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Each channel quantizes its CV source on a trigger (unlike DQ ch0's
    // continuous default), and all four outputs are MAIN pitch (QQ has no aux
    // outputs). Route all four CV inputs to bus 1 and feed a voltage, then drive
    // the four trigger inputs (TR in i defaults to bus 5 + i) high so every
    // channel's Update fires and writes its DAC output. A quantized pitch-0 write
    // is code 32768 (0V), so a fired channel reads non-zero.
    //
    // This asserts the path runs end to end (ISR -> trigger -> quantizer -> DAC),
    // not the exact pitch: the vendor main output routes through a calibrated DAC
    // the shim collapses to an uncalibrated 1V/oct model, so the precise code is
    // not host-meaningful. Pitch fidelity is a hardware-smoke concern.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[0] = v[1] = v[2] = v[3] = 1;  // all CV inputs -> bus 1
    float* cv_bus = nt::bus_pointer(1, numFrames);
    for (int i = 0; i < numFrames; ++i) cv_bus[i] = 2.0f;
    for (int tr = 0; tr < 4; ++tr) {
        float* trig = nt::bus_pointer(5 + tr, numFrames);
        for (int i = 0; i < numFrames; ++i) trig[i] = 5.0f;
    }
    run_steps(p, numFrames, 4);

    int out[4]; qq_get_outputs(out);
    const bool any_output = out[0] || out[1] || out[2] || out[3];
    REQUIRE(any_output);
}

TEST_CASE("Quantermain settings and masks round-trip through serialise/deserialise", "[oc_app][qq][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Mutate a representative set of exposed settings on each channel. apply_value
    // returns false on a no-op, so assert the post-state, not the apply return
    // (BYTEBEATGEN lesson).
    struct W { int ch, setting, value; };
    const W writes[] = {
        {0, P_SCALE, 7}, {0, P_ROOT, 5}, {0, P_TRANSPOSE, 3},
        {1, P_SCALE, 9}, {1, P_OCTAVE, 2},
        {2, P_SOURCE, 1}, {2, P_FINE, 100},
        {3, P_ROOT, 4}, {3, P_CLKDIV, 2},
    };
    for (const W& w : writes) {
        qq_apply_setting(w.ch, w.setting, w.value);
        REQUIRE(qq_get_setting(w.ch, w.setting) == w.value);
    }
    // The excluded masks (not parameters) must persist through the full blob.
    qq_set_mask(0, 0x0F0F);
    qq_set_mask(3, 0x00FF);
    REQUIRE(qq_get_mask(0) == 0x0F0F);
    REQUIRE(qq_get_mask(3) == 0x00FF);

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (const W& w : writes) qq_apply_setting(w.ch, w.setting, 0);
    qq_set_mask(0, 1);
    qq_set_mask(3, 1);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (const W& w : writes)
        REQUIRE(qq_get_setting(w.ch, w.setting) == w.value);
    REQUIRE(qq_get_mask(0) == 0x0F0F);
    REQUIRE(qq_get_mask(3) == 0x00FF);
}

TEST_CASE("Quantermain NT parameter add-on syncs to the right channel and setting", "[oc_app][qq][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = qq_settings_param_base();
    qq_arm_sentinel(p->algorithm);

    // The first row of channel 2's block (flat = 2 * exposed_per_channel) maps to
    // physical SCALE on channel 2.
    const int per = qq_exposed_per_channel();
    const int flat = 2 * per + 0;
    REQUIRE(flat_channel(flat) == 2);
    REQUIRE(flat_to_phys(flat) == P_SCALE);

    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + flat] = 6;
    p->factory->parameterChanged(p->algorithm, base + flat);
    REQUIRE(qq_get_setting(2, P_SCALE) == 6);
    REQUIRE(qq_get_setting(0, P_SCALE) != 6);   // channel 0 untouched (default)
}

TEST_CASE("Quantermain parameterChanged honors the common-parameter offset",
          "[oc_app][qq][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    qq_arm_sentinel(p->algorithm);

    const int base = qq_settings_param_base();
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + P_ROOT] = 7;   // channel 0, within-row P_ROOT (physical 1)
    p->factory->parameterChanged(p->algorithm, base + P_ROOT);
    REQUIRE(qq_get_setting(0, P_ROOT) == 7);
}
