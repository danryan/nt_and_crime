// Dual Quantizer (vendor APP_DQ.h / "Meta-Q") O_C app port. Validates the first
// multi-channel app with a PER-CHANNEL setting subset: two DQ_QuantizerChannel
// instances of 31 settings each, of which the four U16 scale masks (contiguous
// indices 9..12) are excluded per channel, leaving 27 exposed rows per channel
// (54 flat NT rows). The facade is the BBGEN quad-facade shape with N = 2 plus a
// within-channel remap that skips the mask block; the masks persist through the
// whole-app DQ_save/DQ_restore blob.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/DQ.cpp
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

// Test seams defined in plugins/apps/DQ.cpp. Setting seams take a
// (channel, physical-setting) pair so a test can address any setting, including
// the excluded masks.
int  dq_get_setting(int channel, int setting);
bool dq_apply_setting(int channel, int setting, int value);
int  dq_num_channels();
int  dq_settings_per_channel_total();
int  dq_exposed_per_channel();
int  dq_setting_count();
int  dq_settings_param_base();
int  dq_phys_in_channel(int within);
int  dq_get_mask(int channel, int slot);
void dq_set_mask(int channel, int slot, int value);
void dq_get_outputs(int out[4]);
void dq_arm_sentinel(_NT_algorithm* self);

namespace {

// Vendor DQ_ChannelSetting physical indices (APP_DQ.h:58).
enum {
    P_SCALE1 = 0, P_SCALE2, P_SCALE3, P_SCALE4,
    P_ROOT1, P_ROOT2, P_ROOT3, P_ROOT4,
    P_SCALE_SEQ,
    P_MASK1, P_MASK2, P_MASK3, P_MASK4,
    P_SEQ_MODE, P_SOURCE, P_TRIGGER, P_DELAY,
    P_TRANSPOSE1, P_TRANSPOSE2, P_TRANSPOSE3, P_TRANSPOSE4,
    P_OCTAVE, P_AUX_OUTPUT, P_PULSEWIDTH, P_AUX_OCTAVE, P_AUX_CV_DEST,
    P_TURING_LENGTH, P_TURING_PROB, P_TURING_CV_SOURCE, P_TURING_RANGE,
    P_TURING_TRIG_OUT,
    P_LAST,
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

// Flat NT row -> (channel, within); within -> physical via dq_phys_in_channel.
int flat_to_phys(int flat) {
    const int per = dq_exposed_per_channel();
    return dq_phys_in_channel(flat % per);
}
int flat_channel(int flat) { return flat / dq_exposed_per_channel(); }

}  // namespace

TEST_CASE("Dual Quantizer loads through the factory path with a custom UI", "[oc_app][dq][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'D', 'Q'));
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    REQUIRE(dq_num_channels() == 2);
    REQUIRE(dq_settings_per_channel_total() == P_LAST);   // 31
    REQUIRE(dq_exposed_per_channel() == P_LAST - 4);       // 27 (4 masks excluded)
    REQUIRE(dq_setting_count() == 2 * (P_LAST - 4));       // 54
}

TEST_CASE("Dual Quantizer exposes a per-channel subset skipping the four U16 masks", "[oc_app][dq][subset]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // The within-channel remap maps rows 0..8 straight through, then skips the
    // four mask slots (physical 9..12): within-row 9 lands on physical 13.
    REQUIRE(dq_phys_in_channel(8) == P_SCALE_SEQ);   // 8 -> 8
    REQUIRE(dq_phys_in_channel(9) == P_SEQ_MODE);    // 9 -> 13 (masks skipped)
    REQUIRE(dq_phys_in_channel(10) == P_SOURCE);     // 10 -> 14

    // No exposed row maps onto any mask slot.
    const int per = dq_exposed_per_channel();
    for (int w = 0; w < per; ++w) {
        const int phys = dq_phys_in_channel(w);
        REQUIRE((phys < P_MASK1 || phys > P_MASK4));
    }
}

TEST_CASE("Dual Quantizer draw renders the menu", "[oc_app][dq][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Dual Quantizer drives its DAC outputs when stepped", "[oc_app][dq][quantize]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Channel 0 defaults to a continuous trigger, so it quantizes its CV source
    // every ISR with no gate. Route all four CV inputs to bus 1 and feed a
    // voltage, then step: the ISR must run both channels' Update and write the
    // four DAC outputs (ch0 -> A,C; ch1 -> B,D), so the read-back is not all
    // zero.
    //
    // This asserts the audio path runs end to end (ISR -> quantizer -> DAC), not
    // the exact pitch: the vendor main-output path routes the quantizer result
    // through pitch_to_scaled_voltage_dac with a continuous-mode offset against a
    // calibrated DAC, which the shim collapses to an uncalibrated 1V/oct model,
    // so the precise main-channel code is not meaningful on the host (the aux
    // channels settle at the 0V code). Pitch fidelity is a hardware-smoke
    // concern, verified on-device, not in the host sim.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[0] = v[1] = v[2] = v[3] = 1;  // all CV inputs -> bus 1
    float* cv_bus = nt::bus_pointer(1, numFrames);
    for (int i = 0; i < numFrames; ++i) cv_bus[i] = 2.0f;
    run_steps(p, numFrames, 4);

    int out[4]; dq_get_outputs(out);
    const bool any_output = out[0] || out[1] || out[2] || out[3];
    REQUIRE(any_output);
}

TEST_CASE("Dual Quantizer settings and masks round-trip through serialise/deserialise", "[oc_app][dq][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Mutate a representative set of exposed settings on both channels plus a
    // mask per channel. apply_value returns false on a no-op, so assert the
    // post-state, not the apply return (BYTEBEATGEN lesson).
    struct W { int ch, setting, value; };
    const W writes[] = {
        {0, P_SCALE1, 7}, {0, P_ROOT1, 5}, {0, P_TRANSPOSE1, 3}, {0, P_OCTAVE, 2},
        {0, P_TURING_LENGTH, 12},
        {1, P_SCALE1, 9}, {1, P_ROOT2, 4}, {1, P_AUX_OUTPUT, 1}, {1, P_PULSEWIDTH, 40},
    };
    for (const W& w : writes) {
        dq_apply_setting(w.ch, w.setting, w.value);
        REQUIRE(dq_get_setting(w.ch, w.setting) == w.value);
    }
    // The excluded masks (not parameters) must persist through the full blob.
    dq_set_mask(0, 0, 0x0F0F);
    dq_set_mask(1, 3, 0x00FF);
    REQUIRE(dq_get_mask(0, 0) == 0x0F0F);
    REQUIRE(dq_get_mask(1, 3) == 0x00FF);

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (const W& w : writes) dq_apply_setting(w.ch, w.setting, 0);
    dq_set_mask(0, 0, 1);
    dq_set_mask(1, 3, 1);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (const W& w : writes)
        REQUIRE(dq_get_setting(w.ch, w.setting) == w.value);
    REQUIRE(dq_get_mask(0, 0) == 0x0F0F);
    REQUIRE(dq_get_mask(1, 3) == 0x00FF);
}

TEST_CASE("Dual Quantizer NT parameter add-on syncs to the right channel and setting", "[oc_app][dq][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = dq_settings_param_base();
    dq_arm_sentinel(p->algorithm);

    // A flat row in channel 1's block must update channel 1's physical setting.
    // Pick the first row of channel 1 (flat index = exposed_per_channel), which
    // maps to physical SCALE1 on channel 1.
    const int per = dq_exposed_per_channel();
    const int flat = per + 0;                       // channel 1, within-row 0
    REQUIRE(flat_channel(flat) == 1);
    REQUIRE(flat_to_phys(flat) == P_SCALE1);

    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + flat] = 6;
    p->factory->parameterChanged(p->algorithm, base + flat);
    REQUIRE(dq_get_setting(1, P_SCALE1) == 6);
    REQUIRE(dq_get_setting(0, P_SCALE1) != 6);      // channel 0 untouched (default)
}

TEST_CASE("Dual Quantizer parameterChanged honors the common-parameter offset",
          "[oc_app][dq][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    dq_arm_sentinel(p->algorithm);

    const int base = dq_settings_param_base();
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + P_ROOT1] = 7;                          // channel 0, within-row P_ROOT1
    p->factory->parameterChanged(p->algorithm, base + P_ROOT1);
    REQUIRE(dq_get_setting(0, P_ROOT1) == 7);
}
