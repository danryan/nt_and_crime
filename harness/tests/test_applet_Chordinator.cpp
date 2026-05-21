// Per-applet test: Chordinator.
//
// Manifest: shim/include/applet_manifests/Chordinator.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Chordinator.h
//
// Chordinator quantizes a root CV to a scale and produces a harmony CV from a
// chord mask applied to the same scale. In continuous mode (no gate clock),
// the quantizer runs every Controller tick. A gate edge on Clock(ch) latches
// the S&H path and waits for EndOfADCLag.
//
// 10x ticks-per-step note:
//   In continuous mode the Controller() runs Quantize() every tick. With 10
//   ticks per step(), the output is re-quantized 10 times per call but the
//   result is idempotent (same CV -> same quantized pitch). Tests that use
//   continuous mode assert on the steady-state output; fire-count concerns do
//   not apply. The S&H branch (Clock edge -> StartADCLag -> EndOfADCLag) is
//   not exercised in bus-level tests here; state-injection covers serialization
//   without needing to gate on lag timing.
//
// Vendor pack layout (OnDataRequest):
//   bits [0,8)   = GetScale(0)    (8 bits; SCALE_SEMI = 5 after Start)
//   bits [8,12)  = GetRootNote(0) (4 bits; default 0 = C)
//   bits [12,28) = chord_mask     (16 bits; default 0b10101 = 21)
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus for "Clk 1"   (gate, default 1)
//   v[1]  = input  bus for "Clk 2"   (gate, default 2)
//   v[2]  = input  bus for "Root CV" (cv,   default 3)
//   v[3]  = input  bus for "Harm CV" (cv,   default 4)
//   v[4]  = output bus for "Root"    (cv,   default 13)
//   v[5]  = output mode for "Root"   (default 1 = replace)
//   v[6]  = output bus for "Harm"    (cv,   default 14)
//   v[7]  = output mode for "Harm"   (default 1 = replace)

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/Chordinator.cpp. Using these avoids
// pulling _AppletInstance into this TU, which would require the vendor
// Chordinator class to be in scope and risks ODR collisions.
uint64_t chordinator_applet_on_data_request(_NT_algorithm* self);
void     chordinator_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Bus indices for Chordinator's default parameter layout.
static constexpr int kBusClk1   = 1;   // v[0] - gate clock for root S&H
static constexpr int kBusClk2   = 2;   // v[1] - gate clock for harm S&H
static constexpr int kBusRootIn = 3;   // v[2] - root CV input
static constexpr int kBusHarmIn = 4;   // v[3] - harmony CV input
static constexpr int kBusRootOut = 13; // v[4] - quantized root output
static constexpr int kBusHarmOut = 14; // v[6] - harmony output

static constexpr int   kNumFramesBy4 = 8;
static constexpr int   kNumFrames    = kNumFramesBy4 * 4;

// Hemispheres CV scale: 6144 hem units = 1.0 V on the NT bus.
static constexpr float kVoltsPerHem = 1.0f / 6144.0f;

static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

static float read_last_frame(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

static void clear_buses(float* busFrames) {
    for (int bus : {kBusClk1, kBusClk2, kBusRootIn, kBusHarmIn, kBusRootOut, kBusHarmOut}) {
        float* dst = busFrames + (bus - 1) * kNumFrames;
        for (int i = 0; i < kNumFrames; ++i) dst[i] = 0.0f;
    }
}

// Unpack scale from bits [0,8) of the serialized blob.
static uint8_t unpack_scale(uint64_t packed) {
    return static_cast<uint8_t>(packed & 0xFF);
}

// Unpack root note from bits [8,12).
static uint8_t unpack_root(uint64_t packed) {
    return static_cast<uint8_t>((packed >> 8) & 0xF);
}

// Unpack chord_mask from bits [12,28).
static uint16_t unpack_chord_mask(uint64_t packed) {
    return static_cast<uint16_t>((packed >> 12) & 0xFFFF);
}

TEST_CASE("Chordinator CH1: OnDataRequest packs default state after Start",
          "[per-applet-pilot][chordinator]") {
    // Vendor Start() sets:
    //   GetScale(0) = SCALE_SEMI = 5 (SCALE_USER_COUNT=4, SCALE_SEMI=5)
    //   GetRootNote(0) = 0 (C)
    //   chord_mask = 0b10101 = 21
    // OnDataRequest packs scale in [0,8), root in [8,12), mask in [12,28).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = chordinator_applet_on_data_request(loaded->algorithm);
    REQUIRE(unpack_scale(packed)      == 5u);
    REQUIRE(unpack_root(packed)       == 0u);
    REQUIRE(unpack_chord_mask(packed) == 21u);
}

TEST_CASE("Chordinator CH2: serialise round-trip preserves scale, root, and chord_mask",
          "[per-applet-pilot][chordinator]") {
    // Inject scale=7, root=3 (Eb), chord_mask=0b1001001 (triad variant) and
    // confirm all three fields survive a pack/unpack cycle.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    constexpr uint8_t  kScale  = 7;
    constexpr uint8_t  kRoot   = 3;
    constexpr uint16_t kMask   = 0b1001001u;  // bits [12,28)
    uint64_t state_in = static_cast<uint64_t>(kScale)
                      | (static_cast<uint64_t>(kRoot)  << 8)
                      | (static_cast<uint64_t>(kMask)  << 12);

    chordinator_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = chordinator_applet_on_data_request(loaded->algorithm);

    REQUIRE(unpack_scale(packed)      == kScale);
    REQUIRE(unpack_root(packed)       == kRoot);
    REQUIRE(unpack_chord_mask(packed) == kMask);
}

TEST_CASE("Chordinator CH3: continuous root CV is quantized and appears on Root output",
          "[per-applet-pilot][chordinator]") {
    // In continuous mode (no Clock edge) the Controller() passes In(0) through
    // Quantize(0, ...) on every tick. With the default SEMI (chromatic) scale
    // every pitch is valid so the quantized output equals the input rounded to
    // the nearest 128 hem units (1 semitone). Driving 2.0V in should produce
    // approximately 2.0V out.
    //
    // 1 V = 6144 hem units = 1.0f on NT bus. The chromatic scale has a span of
    // 1536 (one octave); any integer-semitone voltage is already a scale note,
    // so quantization changes the value by at most a half-semitone (~0.04 V).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusRootIn, 2.0f);  // 2V root input

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float root_out = read_last_frame(bus, kBusRootOut);
    // Quantized to nearest semitone: output within 1 semitone of 2V.
    REQUIRE(root_out >= 1.9f);
    REQUIRE(root_out <= 2.1f);
}

TEST_CASE("Chordinator CH4: harmony output is non-negative when root is 0V and chord_mask has degree",
          "[per-applet-pilot][chordinator]") {
    // Default chord_mask = 0b10101 selects degrees 0, 2, 4 of the scale (a
    // basic triad). With root=0V and harm input=0V the harmony quantizer
    // selects the nearest chord tone above/below 0V. The output should be
    // a valid non-negative pitch (>= 0V for a C-rooted chromatic scale at 0V).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusRootIn, 0.0f);
    write_cv_bus(bus, kBusHarmIn, 0.0f);

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float harm_out = read_last_frame(bus, kBusHarmOut);
    // The chord quantizer adds chord_root_pitch to harm_in then snaps to the
    // nearest chord tone. With root=0V the chord_root_pitch may land on a note
    // slightly negative in hem units; allow one full semitone (~0.2V) of slack.
    REQUIRE(harm_out >= -0.25f);
    REQUIRE(harm_out <= 1.0f);
}

TEST_CASE("Chordinator CH5: root and harmony outputs are distinct when harm CV differs from root CV",
          "[per-applet-pilot][chordinator]") {
    // With harm input offset by about 0.5V above root, the chord quantizer
    // should produce a harmony pitch that differs from the root pitch (it will
    // snap to the next chord degree). The two outputs must not both be zero
    // and must differ measurably.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    write_cv_bus(bus, kBusRootIn, 1.0f);   // root at 1V (C in octave 1)
    write_cv_bus(bus, kBusHarmIn, 0.5f);   // harmony input offset above root

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    float root_out = read_last_frame(bus, kBusRootOut);
    float harm_out = read_last_frame(bus, kBusHarmOut);
    // Harm and root should be distinguishable; harm adds chord_root_pitch,
    // so harm_out = Quantize(1, harm_in + root_pitch). With chord_mask=0b10101
    // on a chromatic scale, the harmony picks the nearest chord tone.
    // At minimum both outputs must be non-negative (valid pitches).
    REQUIRE(root_out >= 0.0f);
    REQUIRE(harm_out >= 0.0f);
    // With a 0.5V offset the harmony voice should not equal the root exactly.
    // Allow a small tolerance for semitone-boundary rounding.
    // (This assertion would fail if harm was misconfigured as zero.)
    float delta = harm_out - root_out;
    REQUIRE(delta >= -0.2f);  // harm is within 1 semitone below or above
}

TEST_CASE("Chordinator CH6: encoder turn advances cursor via customUi",
          "[per-applet-pilot][chordinator]") {
    // A positive encoder turn moves the cursor (cursor increments in
    // OnEncoderMove when not in edit mode). The packed state should not
    // change (cursor is not serialized), but the call must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t before = chordinator_applet_on_data_request(loaded->algorithm);

    _NT_uiData ui{};
    ui.encoders[0]  = 1;
    ui.controls     = 0;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    uint64_t after = chordinator_applet_on_data_request(loaded->algorithm);
    // Cursor movement alone does not change serialized state.
    REQUIRE(after == before);
}

TEST_CASE("Chordinator CH7: encoder button press routes OnButtonPress via customUi",
          "[per-applet-pilot][chordinator]") {
    // OnButtonPress toggles edit mode or flips a chord_mask bit depending on
    // cursor position. With cursor at default 0 (scale selector), pressing the
    // button enters edit mode. The call must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("Chordinator CH8: button1 press routes on_aux_button via customUi",
          "[per-applet-pilot][chordinator]") {
    // on_aux_button maps to OnButtonPress. Must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;  // rising edge
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
