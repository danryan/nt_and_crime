// Per-applet test: EnsOscKey.
//
// Manifest: shim/include/applet_manifests/EnsOscKey.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/EnsOscKey.h
//
// EnsOscKey is a quantizer + chord-quality annotator. It takes a pitch CV on
// In(0), quantizes it to the selected scale/root, outputs the quantized note on
// Out(0), and outputs a chord-quality CV on Out(1) (major/minor/diminished).
// A gate on In(2) triggers Clock(0), which switches from continuous to
// sample-and-hold mode. In(1) provides an octave CV offset.
//
// 10x ticks-per-step concern: EnsOscKey calls StartADCLag(0) inside Clock(0)
// and only fires output inside `continuous || EndOfADCLag(0)`. In continuous
// mode (the default after Start) the output fires every tick regardless of the
// multiplier, so bus-level output assertions are safe in continuous mode.
// Clock-mode tests use state-injection only to avoid the EndOfADCLag hazard.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input bus for "Pitch"   (default 1)
//   v[1]  = input bus for "Octave"  (default 2)
//   v[2]  = input bus for "Clock"   (default 3)
//   v[3]  = output bus for "Note"   (default 13)
//   v[4]  = output mode for "Note"  (default 1 = replace)
//   v[5]  = output bus for "Scale"  (default 14)
//   v[6]  = output mode for "Scale" (default 1 = replace)
//
// OnDataRequest bit layout (from vendor OnDataRequest):
//   bits [ 0, 8) = scale       (default 6 = Ionian)
//   bits [ 8, 4) = octave + 5  (default 5 = octave 0)
//   bits [12, 4) = voltage_maj (default 3)
//   bits [16, 4) = voltage_min (default 4)
//   bits [20, 4) = voltage_dim (default 5)
//   bits [24, 4) = voltage_no_match (default 6)
//   bits [28, 4) = root        (default 0)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/EnsOscKey.cpp.
uint64_t ensosckey_applet_on_data_request(_NT_algorithm* self);
void     ensosckey_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices from the manifest parameter layout.
static constexpr int kBusPitch  = 1;   // v[0] default - CV input
static constexpr int kBusOctave = 2;   // v[1] default - CV input
static constexpr int kBusClock  = 3;   // v[2] default - gate input
static constexpr int kBusNote   = 13;  // v[3] default - CV output
static constexpr int kBusScale  = 14;  // v[5] default - CV output

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static float read_cv_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_all_buses(float* busFrames) {
    for (int bus : {kBusPitch, kBusOctave, kBusClock, kBusNote, kBusScale}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

TEST_CASE("EnsOscKey EK1: OnDataRequest packs defaults after Start", "[ensosckey]") {
    // Start() sets: scale=6, octave=0(+5=5), voltage_maj=3, voltage_min=4,
    // voltage_dim=5, voltage_no_match=6, root=0.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = ensosckey_applet_on_data_request(loaded->algorithm);

    // scale in bits [0,8)
    REQUIRE(((packed >> 0) & 0xFF) == 6u);
    // octave+5 in bits [8,4)
    REQUIRE(((packed >> 8) & 0xF) == 5u);
    // voltage_maj in bits [12,4)
    REQUIRE(((packed >> 12) & 0xF) == 3u);
    // voltage_min in bits [16,4)
    REQUIRE(((packed >> 16) & 0xF) == 4u);
    // voltage_dim in bits [20,4)
    REQUIRE(((packed >> 20) & 0xF) == 5u);
    // voltage_no_match in bits [24,4)
    REQUIRE(((packed >> 24) & 0xF) == 6u);
    // root in bits [28,4)
    REQUIRE(((packed >> 28) & 0xF) == 0u);
}

TEST_CASE("EnsOscKey EK2: serialise round-trip preserves all fields", "[ensosckey]") {
    // Encode scale=7 (Dorian), octave=-2(+5=3), voltage_maj=5, voltage_min=6,
    // voltage_dim=7, voltage_no_match=8, root=3 and verify round-trip.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = 0;
    state_in |= (uint64_t)7u  << 0;   // scale=7
    state_in |= (uint64_t)3u  << 8;   // octave+5=3 (octave=-2)
    state_in |= (uint64_t)5u  << 12;  // voltage_maj=5
    state_in |= (uint64_t)6u  << 16;  // voltage_min=6
    state_in |= (uint64_t)7u  << 20;  // voltage_dim=7
    state_in |= (uint64_t)8u  << 24;  // voltage_no_match=8
    state_in |= (uint64_t)3u  << 28;  // root=3

    ensosckey_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = ensosckey_applet_on_data_request(loaded->algorithm);

    REQUIRE(((packed >> 0)  & 0xFF) == 7u);
    REQUIRE(((packed >> 8)  & 0xF)  == 3u);
    REQUIRE(((packed >> 12) & 0xF)  == 5u);
    REQUIRE(((packed >> 16) & 0xF)  == 6u);
    REQUIRE(((packed >> 20) & 0xF)  == 7u);
    REQUIRE(((packed >> 24) & 0xF)  == 8u);
    REQUIRE(((packed >> 28) & 0xF)  == 3u);
}

TEST_CASE("EnsOscKey EK3: continuous mode outputs non-zero note CV for non-zero pitch", "[ensosckey]") {
    // In continuous mode (default after Start), the Controller fires every tick.
    // With a non-zero pitch input, Out(0) should produce a non-zero note CV.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);
    // 1V pitch input: quantizes to a note in scale.
    write_cv_bus(bus, kBusPitch, 1.0f);
    write_cv_bus(bus, kBusOctave, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float note_out = read_cv_bus_last(bus, kBusNote);
    REQUIRE(note_out != 0.0f);
}

TEST_CASE("EnsOscKey EK4: continuous mode outputs scale CV on Out(1)", "[ensosckey]") {
    // With scale=6 (Ionian) and root=0, a pitch at 0V (C) maps to root (interval=0),
    // which is a Major interval -> chord quality CV = voltageToCode(voltage_maj).
    // voltage_maj default=3: code = (int)((3.0/2.0 - 0.25) * ONE_OCTAVE)
    // ONE_OCTAVE = 1536 hem units. code = (1.5 - 0.25) * 1536 = 1.25 * 1536 = 1920 hem units.
    // In volts: 1920 / 1536 = 1.25V. Out(1) should be positive (~1.25V).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_all_buses(bus);
    // 0V pitch input quantizes to root note.
    write_cv_bus(bus, kBusPitch, 0.0f);
    write_cv_bus(bus, kBusOctave, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float scale_out = read_cv_bus_last(bus, kBusScale);
    // Major interval -> positive voltage output.
    REQUIRE(scale_out > 0.0f);
}

TEST_CASE("EnsOscKey EK5: encoder turn changes scale setting", "[ensosckey]") {
    // Default scale=6 (Ionian). Encoder turn +1 via customUi increments cursor,
    // then another press enters edit, then turn changes scale to 7 (Dorian).
    // Simpler: move cursor to scale position (cursor=1) then turn in edit mode.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Confirm starting scale=6.
    uint64_t packed = ensosckey_applet_on_data_request(loaded->algorithm);
    REQUIRE(((packed >> 0) & 0xFF) == 6u);

    // Move cursor to position 1 (scale) via encoder turn (cursor starts at 0).
    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Press encoder button to enter edit mode.
    _NT_uiData ui_press{};
    ui_press.encoders[0]  = 0;
    ui_press.controls     = kNT_encoderButtonL;
    ui_press.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui_press);

    // Turn encoder +1: scale increments from 6 to 7.
    _NT_uiData ui_turn{};
    ui_turn.encoders[0] = 1;
    ui_turn.controls    = 0;
    ui_turn.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui_turn);

    uint64_t packed_after = ensosckey_applet_on_data_request(loaded->algorithm);
    REQUIRE(((packed_after >> 0) & 0xFF) == 7u);
}

TEST_CASE("EnsOscKey EK6: encoder button press does not crash", "[ensosckey]") {
    // EnsOscKey's OnButtonPress is commented out (no-op base class). Confirm
    // routing does not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("EnsOscKey EK7: aux button press does not crash", "[ensosckey]") {
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
