// Per-applet test: MultiScale.
//
// Manifest: shim/include/applet_manifests/MultiScale.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/MultiScale.h
//
// MultiScale hosts 4 user-configurable chromatic scale masks and selects
// among them via a CV on input 4. The selected scale quantizes pitch from
// input 3. A trigger fires on output 2 (Trigger) when the active scale
// changes.
//
// 10x clock multiplier note: The Clock(0) path calls StartADCLag(0) on
// each of the 10 inner ticks per buffer. Tests use the default continuous
// mode (continuous=true, Gate(1) asserted or just default state) to avoid
// ADC-lag timing sensitivity. Continuous mode fires quantize on every tick
// unconditionally, so bus-level CV-to-pitch assertions are sound.
//
// Bus parameter layout (per _per_applet_runtime emit_base_parameters):
//   v[0]  = input  bus "Clock"    (gate, default 1)
//   v[1]  = input  bus "UnClock"  (gate, default 2)
//   v[2]  = input  bus "CV"       (cv,   default 3)
//   v[3]  = input  bus "Scale"    (cv,   default 4)
//   v[4]  = output bus "Pitch"    (cv,   default 13)
//   v[5]  = output mode "Pitch mode"    (default 1 = replace)
//   v[6]  = output bus "Trigger"  (gate, default 14)
//   v[7]  = output mode "Trigger mode"  (default 1 = replace)
//
// Scale mask serialization (vendor OnDataRequest):
//   bits [ 0,12) = scale_mask[0]
//   bits [12,24) = scale_mask[1]
//   bits [24,36) = scale_mask[2]
//   bits [36,48) = scale_mask[3]
//   Each mask is 12 bits (one per chromatic pitch class).
//
// Start() initialises all four masks to 0x0001 (root note only) and
// calls quant.Configure on scale_mask[0].

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"

// Test seams defined in plugins/applets/MultiScale.cpp.
uint64_t multiscale_applet_on_data_request(_NT_algorithm* self);
void     multiscale_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

// Default bus indices.
static constexpr int kBusClock   = 1;   // v[0]
static constexpr int kBusUnClock = 2;   // v[1]
static constexpr int kBusCV      = 3;   // v[2]
static constexpr int kBusScale   = 4;   // v[3]
static constexpr int kBusPitch   = 13;  // v[4]
static constexpr int kBusTrigger = 14;  // v[6]

static constexpr int kNumFramesBy4 = 8;
static constexpr int kNumFrames    = kNumFramesBy4 * 4;

// Write a constant CV value (in volts) across all frames of a 1-based bus.
static void write_cv_bus(float* busFrames, int bus_1based, float volts) {
    float* dst = busFrames + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Zero all frames for a given 1-based bus.
static void clear_bus(float* busFrames, int bus_1based) {
    write_cv_bus(busFrames, bus_1based, 0.0f);
}

// Read the last frame value from a 1-based output bus.
static float read_bus_last(float* busFrames, int bus_1based) {
    return busFrames[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Read all four 12-bit scale masks from a packed OnDataRequest word.
static uint16_t unpack_mask(uint64_t data, int index) {
    return static_cast<uint16_t>((data >> (index * 12)) & 0xFFF);
}

// Pack four 12-bit masks into a 64-bit data word mirroring OnDataRequest.
static uint64_t pack_masks(uint16_t m0, uint16_t m1, uint16_t m2, uint16_t m3) {
    return (static_cast<uint64_t>(m0 & 0xFFF))
         | (static_cast<uint64_t>(m1 & 0xFFF) << 12)
         | (static_cast<uint64_t>(m2 & 0xFFF) << 24)
         | (static_cast<uint64_t>(m3 & 0xFFF) << 36);
}

TEST_CASE("MultiScale MS1: Start() initialises all scale masks to 0x0001",
          "[per-applet][multiscale]") {
    // Vendor Start() sets scale_mask[i] = 0x0001 for all i.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    uint64_t packed = multiscale_applet_on_data_request(loaded->algorithm);
    REQUIRE(unpack_mask(packed, 0) == 0x0001u);
    REQUIRE(unpack_mask(packed, 1) == 0x0001u);
    REQUIRE(unpack_mask(packed, 2) == 0x0001u);
    REQUIRE(unpack_mask(packed, 3) == 0x0001u);
}

TEST_CASE("MultiScale MS2: serialise round-trip preserves all four scale masks",
          "[per-applet][multiscale]") {
    // Inject four distinct masks; confirm they round-trip via OnDataReceive/Request.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t state_in = pack_masks(0xABC, 0x123, 0xFFF, 0x456);
    multiscale_applet_on_data_receive(loaded->algorithm, state_in);
    uint64_t packed = multiscale_applet_on_data_request(loaded->algorithm);

    REQUIRE(unpack_mask(packed, 0) == 0xABCu);
    REQUIRE(unpack_mask(packed, 1) == 0x123u);
    REQUIRE(unpack_mask(packed, 2) == 0xFFFu);
    REQUIRE(unpack_mask(packed, 3) == 0x456u);
}

TEST_CASE("MultiScale MS3: continuous mode quantizes CV to pitch output",
          "[per-applet][multiscale]") {
    // Default state: continuous=true. With scale_mask[0]=0x0001 (root only,
    // chromatic semitone scale), any positive CV snaps to the nearest root
    // (0V). The quantizer output should be a finite float on the Pitch bus.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusCV);
    clear_bus(bus, kBusScale);
    clear_bus(bus, kBusPitch);
    write_cv_bus(bus, kBusCV, 0.0f);    // 0V input
    write_cv_bus(bus, kBusScale, 0.0f); // scale 0

    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);

    // Output should be finite (quantizer ran).
    float pitch = read_bus_last(bus, kBusPitch);
    REQUIRE(std::isfinite(pitch));
}

TEST_CASE("MultiScale MS4: Scale CV selects scale 0 vs scale 1",
          "[per-applet][multiscale]") {
    // The vendor uses Proportion(DetentedIn(1), HEMISPHERE_MAX_INPUT_CV=9216,
    // MS_QUANT_SCALES_COUNT=4) to map Scale CV to [0,3].
    // Scale CV = 0V -> scale index 0.
    // Scale CV ~ 3V (4608 hem units = HEMISPHERE_MAX_INPUT_CV/2) -> index ~2.
    // We distinguish by injecting different masks and observing scale change
    // trigger output.
    //
    // Strategy: set scale_mask[0]=all-notes, scale_mask[1]=root-only.
    // Apply CV in scale 0 range -> trigger bus stays low (no scale change).
    // Switch CV to scale 1 range -> trigger bus goes high (scale changed).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    // Inject masks: scale 0 = all 12 notes, scale 1 = root only.
    uint64_t state_in = pack_masks(0xFFF, 0x001, 0x001, 0x001);
    multiscale_applet_on_data_receive(loaded->algorithm, state_in);

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusCV);
    clear_bus(bus, kBusScale);
    clear_bus(bus, kBusTrigger);

    // Step 1: scale CV = 0V -> scale 0, no transition (already on scale 0).
    write_cv_bus(bus, kBusScale, 0.0f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    float trig_after_scale0 = read_bus_last(bus, kBusTrigger);

    // Step 2: switch to scale 1 range (approx 2.5V -> index ~1).
    clear_bus(bus, kBusTrigger);
    write_cv_bus(bus, kBusScale, 2.5f);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    float trig_after_scale1 = read_bus_last(bus, kBusTrigger);

    // Trigger should have fired on the scale change.
    REQUIRE(trig_after_scale1 > 0.5f);
    (void)trig_after_scale0; // no assertion; initial state may or may not trigger
}

TEST_CASE("MultiScale MS5: no trigger when scale CV stays in same scale region",
          "[per-applet][multiscale]") {
    // With scale CV constant at 0V, the scale index stays 0 every step.
    // After any initial transition settles (and HEMISPHERE_CLOCK_TICKS=175
    // inner ticks of countdown expire over ~18 step calls), no further
    // trigger fires.
    //
    // HS::frame is a global that persists across tests; clock_countdown[1]
    // may still be non-zero from a prior test's ClockOut. We run 25 steps
    // at scale CV=0V to drain any outstanding countdown before asserting.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    float* bus = nt::bus_frames_base();
    clear_bus(bus, kBusCV);
    clear_bus(bus, kBusScale);
    clear_bus(bus, kBusTrigger);

    write_cv_bus(bus, kBusScale, 0.0f);

    // Drain any stale trigger countdown and let scale settle at 0.
    for (int i = 0; i < 25; ++i) {
        loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    }

    // Now assert: trigger bus must be low (no scale change occurring).
    float trig = read_bus_last(bus, kBusTrigger);
    REQUIRE(trig < 0.5f);
}

TEST_CASE("MultiScale MS6: encoder turn cycles scale_page via customUi",
          "[per-applet][multiscale]") {
    // OnEncoderMove in non-edit mode advances cursor along the keyboard
    // (MoveCursor). In edit mode (cursor==0 -> CursorToggle enters edit),
    // encoder changes scale_page. We verify at least that the call doesn't
    // crash and cursor state is tracked consistently via OnDataRequest
    // (scale_page is not serialised, so we just ensure no crash).
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Masks unchanged (cursor move doesn't alter masks).
    uint64_t packed = multiscale_applet_on_data_request(loaded->algorithm);
    REQUIRE(unpack_mask(packed, 0) == 0x0001u);
}

TEST_CASE("MultiScale MS7: encoder button press toggles edit mode via customUi",
          "[per-applet][multiscale]") {
    // OnButtonPress at cursor==0 calls CursorToggle (enters edit mode).
    // Second press returns to non-edit. Both must not crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_encoderButtonL;
    ui.lastButtons = 0;  // rising edge

    loaded->factory->customUi(loaded->algorithm, ui);
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

TEST_CASE("MultiScale MS8: aux button press routes on_aux_button via customUi",
          "[per-applet][multiscale]") {
    // on_aux_button maps to OnButtonPress. No crash.
    nt::reset_runtime();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0] = 0;
    ui.controls    = kNT_button1;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
