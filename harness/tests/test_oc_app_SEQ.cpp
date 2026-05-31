// Sequins (vendor APP_SEQ.h / "Sequins") O_C app port. Validates the dual-channel
// step sequencer at N = 2 with a contiguous five-setting per-channel subset: two
// SEQ_Channel instances of 58 settings each, of which the five U16 masks at
// contiguous indices 10..14 (SCALE_MASK plus the four sequence masks MASK1..MASK4)
// are excluded per channel, leaving 53 exposed rows per channel (106 flat NT
// rows). The facade is the DQ multi-channel shape with N = 2 plus a within-channel
// remap that skips the mask block; the masks persist through the whole-app
// SEQ_save/SEQ_restore blob.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/SEQ.cpp
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

// Test seams defined in plugins/apps/SEQ.cpp. Setting seams take a
// (channel, physical-setting) pair so a test can address any setting, including
// the excluded masks.
int  sq_get_setting(int channel, int setting);
bool sq_apply_setting(int channel, int setting, int value);
int  sq_num_channels();
int  sq_settings_per_channel_total();
int  sq_exposed_per_channel();
int  sq_setting_count();
int  sq_settings_param_base();
int  sq_phys_in_channel(int within);
int  sq_mask_first();
int  sq_num_masks();
int  sq_get_mask(int channel, int slot);
void sq_set_mask(int channel, int slot, int value);
void sq_get_outputs(int out[4]);
void sq_arm_sentinel(_NT_algorithm* self);

namespace {

// Vendor SEQ_ChannelSetting physical indices (APP_SEQ.h:134). Only the rows the
// tests address are named; LAST is the table size.
enum {
    P_MODE = 0, P_CLOCK, P_TRIGGER_DELAY, P_RESET, P_MULT, P_PULSEWIDTH,
    P_SCALE, P_OCTAVE, P_ROOT, P_OCTAVE_AUX,
    P_SCALE_MASK,                       // 10, first excluded U16 mask
    P_MASK1, P_MASK2, P_MASK3, P_MASK4, // 11..14, the four sequence masks
    P_SEQUENCE,                         // 15, first row after the mask block
    P_SEQUENCE_LEN1,                    // 16
};
constexpr int P_LAST = 58;              // SEQ_CHANNEL_SETTING_LAST

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

// Flat NT row -> (channel, within); within -> physical via sq_phys_in_channel.
int flat_to_phys(int flat) {
    const int per = sq_exposed_per_channel();
    return sq_phys_in_channel(flat % per);
}
int flat_channel(int flat) { return flat / sq_exposed_per_channel(); }

}  // namespace

TEST_CASE("Sequins loads through the factory path with a custom UI", "[oc_app][seq][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory != nullptr);
    REQUIRE(p->algorithm != nullptr);

    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'S', 'Q'));
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    REQUIRE(sq_num_channels() == 2);
    REQUIRE(sq_settings_per_channel_total() == P_LAST);    // 58
    REQUIRE(sq_mask_first() == P_SCALE_MASK);              // 10
    REQUIRE(sq_num_masks() == 5);                          // SCALE_MASK + MASK1..4
    REQUIRE(sq_exposed_per_channel() == P_LAST - 5);       // 53 (5 masks excluded)
    REQUIRE(sq_setting_count() == 2 * (P_LAST - 5));       // 106
}

TEST_CASE("Sequins exposes a per-channel subset skipping the five contiguous U16 masks", "[oc_app][seq][subset]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // The within-channel remap maps rows 0..9 straight through, then skips the
    // five mask slots (physical 10..14): within-row 10 lands on physical 15.
    REQUIRE(sq_phys_in_channel(9) == P_OCTAVE_AUX);   // 9 -> 9
    REQUIRE(sq_phys_in_channel(10) == P_SEQUENCE);    // 10 -> 15 (masks skipped)
    REQUIRE(sq_phys_in_channel(11) == P_SEQUENCE_LEN1); // 11 -> 16

    // No exposed row maps onto any mask slot.
    const int per = sq_exposed_per_channel();
    for (int w = 0; w < per; ++w) {
        const int phys = sq_phys_in_channel(w);
        REQUIRE((phys < P_SCALE_MASK || phys > P_MASK4));
    }
}

TEST_CASE("Sequins draw renders the menu", "[oc_app][seq][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("Sequins drives its DAC outputs when stepped", "[oc_app][seq][sequence]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Channel 0 is clocked from TR1 (DIGITAL_INPUT_1) and channel 1 from TR3
    // (DIGITAL_INPUT_3). Route all four CV inputs to bus 1 and feed a voltage,
    // then drive the four trigger inputs (TR in i defaults to bus 5 + i) high so
    // both channels advance and write their main + aux DAC outputs (ch0 -> A,C;
    // ch1 -> B,D). A main-pitch-0 write is code 32768 (0V), so a stepped channel
    // reads non-zero.
    //
    // This asserts the path runs end to end (ISR -> trigger -> sequencer -> DAC),
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

    int out[4]; sq_get_outputs(out);
    const bool any_output = out[0] || out[1] || out[2] || out[3];
    REQUIRE(any_output);
}

TEST_CASE("Sequins settings and masks round-trip through serialise/deserialise", "[oc_app][seq][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Mutate a representative set of exposed settings on both channels plus a mask
    // per channel. apply_value returns false on a no-op, so assert the post-state,
    // not the apply return (BYTEBEATGEN lesson).
    struct W { int ch, setting, value; };
    const W writes[] = {
        {0, P_MODE, 2}, {0, P_ROOT, 5}, {0, P_OCTAVE, 2}, {0, P_MULT, 20},
        {1, P_SCALE, 3}, {1, P_PULSEWIDTH, 40}, {1, P_TRIGGER_DELAY, 1},
    };
    for (const W& w : writes) {
        sq_apply_setting(w.ch, w.setting, w.value);
        REQUIRE(sq_get_setting(w.ch, w.setting) == w.value);
    }
    // The excluded masks (not parameters) must persist through the full blob: the
    // scale mask (slot 0) on channel 0 and sequence mask 4 (slot 4) on channel 1.
    sq_set_mask(0, 0, 0x0F0F);
    sq_set_mask(1, 4, 0x00FF);
    REQUIRE(sq_get_mask(0, 0) == 0x0F0F);
    REQUIRE(sq_get_mask(1, 4) == 0x00FF);

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (const W& w : writes) sq_apply_setting(w.ch, w.setting, 0);
    sq_set_mask(0, 0, 1);
    sq_set_mask(1, 4, 1);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (const W& w : writes)
        REQUIRE(sq_get_setting(w.ch, w.setting) == w.value);
    REQUIRE(sq_get_mask(0, 0) == 0x0F0F);
    REQUIRE(sq_get_mask(1, 4) == 0x00FF);
}

TEST_CASE("Sequins NT parameter add-on syncs to the right channel and setting", "[oc_app][seq][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = sq_settings_param_base();
    sq_arm_sentinel(p->algorithm);

    // A flat row in channel 1's block must update channel 1's physical setting.
    // The first row of channel 1 (flat index = exposed_per_channel) maps to
    // physical MODE on channel 1.
    const int per = sq_exposed_per_channel();
    const int flat = per + 0;                       // channel 1, within-row 0
    REQUIRE(flat_channel(flat) == 1);
    REQUIRE(flat_to_phys(flat) == P_MODE);

    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + flat] = 2;
    p->factory->parameterChanged(p->algorithm, base + flat);
    REQUIRE(sq_get_setting(1, P_MODE) == 2);
    REQUIRE(sq_get_setting(0, P_MODE) != 2);        // channel 0 untouched (default)
}

TEST_CASE("Sequins parameterChanged honors the common-parameter offset",
          "[oc_app][seq][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    sq_arm_sentinel(p->algorithm);

    const int base = sq_settings_param_base();
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + P_ROOT] = 7;                           // channel 0, within-row P_ROOT
    p->factory->parameterChanged(p->algorithm, base + P_ROOT);
    REQUIRE(sq_get_setting(0, P_ROOT) == 7);
}
