// Analog Shift Register (vendor APP_ASR.h) O_C app port. Validates a
// single-instance subset app (the PASSENCORE template) that also pulls the
// integer-sequence digit-table subsystem: 26 of 27 settings are NT parameters,
// the U16 scale mask (index 3) excluded and edited through the scale-editor
// customUI; the mask persists through the SettingsBase blob.
//
// This test does NOT aggregate the OC shim impl. Only plugins/apps/ASR.cpp
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

// Test seams defined in plugins/apps/ASR.cpp (logical row -> physical, mask
// skipped).
int  asr_get_setting(_NT_algorithm* self, int logical);
bool asr_apply_setting(_NT_algorithm* self, int logical, int value);
int  asr_setting_count();
int  asr_settings_param_base();
int  asr_get_mask();
void asr_set_mask(int value);
void asr_get_outputs(int out[4]);
void asr_arm_sentinel(_NT_algorithm* self);

namespace {

// Logical exposed-row numbering (physical ASRSettings minus MASK at index 3).
enum {
    L_SCALE = 0,
    L_OCTAVE,
    L_ROOT,
    L_INDEX,            // physical 4 (MASK at 3 skipped)
    L_MULT,
    L_DELAY,
    L_BUFFER_LENGTH,
    L_CV_SOURCE,
    L_CV4_DESTINATION,
    L_SLEW,
    L_SLEW_CV,
    L_TURING_LENGTH,
    L_TURING_PROB,
    L_TURING_CV_SOURCE,
    L_BYTEBEAT_EQUATION,
    L_BYTEBEAT_P0,
    L_BYTEBEAT_P1,
    L_BYTEBEAT_P2,
    L_BYTEBEAT_CV_SOURCE,
    L_INT_SEQ_INDEX,
    L_INT_SEQ_MODULUS,
    L_INT_SEQ_START,
    L_INT_SEQ_LENGTH,
    L_INT_SEQ_DIR,
    L_FRACTAL_SEQ_STRIDE,
    L_INT_SEQ_CV_SOURCE,
    L_LAST,
};

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

TEST_CASE("ASR loads through the factory path with a custom UI", "[oc_app][asr][factory]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    REQUIRE(p->factory->guid == NT_MULTICHAR('O', 'C', 'A', 'S'));
    REQUIRE(p->factory->customUi != nullptr);
    REQUIRE(p->factory->serialise != nullptr);
    REQUIRE(p->factory->hasCustomUi(p->algorithm) != 0u);

    REQUIRE(asr_setting_count() == L_LAST);
    REQUIRE(L_LAST == 26);
}

TEST_CASE("ASR exposes a contiguous subset skipping the U16 mask", "[oc_app][asr][subset]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    // Logical row 3 maps to ASR_SETTING_INDEX (physical 4), not the mask at
    // physical 3. INDEX is a small U8 (0..ASR_HOLD_BUF_SIZE-1); a write that
    // round-trips on logical 3 proves the remap reaches INDEX and never the mask.
    REQUIRE(asr_apply_setting(p->algorithm, L_INDEX, 2));
    REQUIRE(asr_get_setting(p->algorithm, L_INDEX) == 2);

    // The mask keeps its U16 default, untouched by the logical-row writes.
    REQUIRE(asr_get_mask() == 65535);
}

TEST_CASE("ASR draw renders the menu", "[oc_app][asr][draw]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const bool suppress = p->factory->draw(p->algorithm);
    REQUIRE(suppress == true);
    REQUIRE(count_nonzero_screen() > 0);
}

TEST_CASE("ASR drives its DAC outputs when clocked", "[oc_app][asr][shift]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int numFrames = 32;
    nt::set_bus_frame_count(numFrames);

    // Route the clock to trigger input 1 (bus 1) and the CV source to all CV
    // inputs (bus 2) with a voltage. Pulse the clock several times so the shift
    // register samples and the four DAC stages fill.
    //
    // Asserts the ISR -> quantizer -> shift-register -> DAC path runs (outputs
    // not all zero), not exact pitch: like DQ, the quantized main output routes
    // through the calibrated-DAC path the shim collapses to 1V/oct, so precise
    // codes are a hardware-smoke concern.
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[kTrigInBase + 0] = 1;          // clock -> bus 1
    v[0] = v[1] = v[2] = v[3] = 2;   // CV inputs -> bus 2
    float* clk = nt::bus_pointer(1, numFrames);
    float* cv  = nt::bus_pointer(2, numFrames);
    for (int i = 0; i < numFrames; ++i) cv[i] = 1.5f;

    for (int pulse = 0; pulse < 5; ++pulse) {
        for (int i = 0; i < numFrames; ++i) clk[i] = 0.0f;
        run_steps(p, numFrames, 1);
        for (int i = 0; i < numFrames; ++i) clk[i] = 5.0f;
        run_steps(p, numFrames, 1);
    }

    int out[4]; asr_get_outputs(out);
    const bool any_output = out[0] || out[1] || out[2] || out[3];
    REQUIRE(any_output);
}

TEST_CASE("ASR settings and mask round-trip through serialise/deserialise", "[oc_app][asr][settings]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    REQUIRE(asr_setting_count() == 26);

    // In-range, non-default values per exposed row. apply_value returns false on
    // a no-op, so assert the post-state (BYTEBEATGEN lesson).
    const int written[L_LAST] = {
        /*SCALE*/ 7, /*OCTAVE*/ 2, /*ROOT*/ 5, /*INDEX*/ 3, /*MULT*/ 10,
        /*DELAY*/ 2, /*BUFLEN*/ 6, /*CV_SOURCE*/ 1, /*CV4_DEST*/ 1, /*SLEW*/ 30,
        /*SLEW_CV*/ 1, /*TUR_LEN*/ 12, /*TUR_PROB*/ 100, /*TUR_CV*/ 1,
        /*BB_EQ*/ 3, /*BB_P0*/ 20, /*BB_P1*/ 30, /*BB_P2*/ 40, /*BB_CV*/ 1,
        /*ISEQ_IDX*/ 2, /*ISEQ_MOD*/ 30, /*ISEQ_START*/ 4, /*ISEQ_LEN*/ 6,
        /*ISEQ_DIR*/ 0, /*FRACT*/ 3, /*ISEQ_CV*/ 1,
    };
    for (int i = 0; i < L_LAST; ++i) {
        asr_apply_setting(p->algorithm, i, written[i]);
        REQUIRE(asr_get_setting(p->algorithm, i) == written[i]);
    }
    asr_set_mask(0x0F0F);
    REQUIRE(asr_get_mask() == 0x0F0F);

    auto stream = nt::make_json_stream();
    stream->openObject();
    p->factory->serialise(p->algorithm, *stream);
    stream->closeObject();
    const std::string json = stream->buffer();
    REQUIRE(json.find("oc_len") != std::string::npos);

    for (int i = 0; i < L_LAST; ++i) asr_apply_setting(p->algorithm, i, 0);
    asr_set_mask(1);

    auto parse = nt::make_json_parse(json);
    REQUIRE(p->factory->deserialise(p->algorithm, *parse) == true);

    for (int i = 0; i < L_LAST; ++i)
        REQUIRE(asr_get_setting(p->algorithm, i) == written[i]);
    REQUIRE(asr_get_mask() == 0x0F0F);
}

TEST_CASE("ASR NT parameter add-on syncs bidirectionally", "[oc_app][asr][param-sync]") {
    nt::reset_runtime();
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);

    const int base = asr_settings_param_base();
    asr_arm_sentinel(p->algorithm);

    // Direction 1: NT parameter -> app value. OCTAVE (logical 1, range -5..5).
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + L_OCTAVE] = 2;
    p->factory->parameterChanged(p->algorithm, base + L_OCTAVE);
    REQUIRE(asr_get_setting(p->algorithm, L_OCTAVE) == 2);

    // Direction 2: app encoder edit -> NT store. The vendor's scrollable cursor
    // list starts at ROOT (SCALE and OCTAVE are steered by the LEFT encoder, not
    // the right-encoder cursor; the mask row, one step down, opens the scale
    // editor on BUTTON_R). At the starting cursor position the enabled setting is
    // ROOT, a plain right-encoder-edited row, so a short BUTTON_R press/release
    // toggles editing and ENCODER_R edits ROOT (logical 2). The runtime push-back
    // must mirror it onto v[base + ROOT].
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(kNT_encoderButtonR, 0, 0, 0));
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, kNT_encoderButtonR, 0, 0));
    const int before = asr_get_setting(p->algorithm, L_ROOT);
    p->factory->customUi(p->algorithm, oc_ui_sim::make_uidata(0, 0, /*enc_l=*/0, /*enc_r=*/1));
    const int after = asr_get_setting(p->algorithm, L_ROOT);
    REQUIRE(after == before + 1);
    REQUIRE(p->algorithm->v[base + L_ROOT] == after);
}

TEST_CASE("ASR parameterChanged honors the common-parameter offset",
          "[oc_app][asr][param-sync][offset]") {
    nt::reset_runtime();
    nt::set_parameter_offset(1);
    nt::LoadedPlugin* p = nt::load_plugin();
    REQUIRE(p != nullptr);
    asr_arm_sentinel(p->algorithm);

    const int base = asr_settings_param_base();
    int16_t* v = const_cast<int16_t*>(p->algorithm->v);
    v[base + L_ROOT] = 5;
    p->factory->parameterChanged(p->algorithm, base + L_ROOT);
    REQUIRE(asr_get_setting(p->algorithm, L_ROOT) == 5);
}
