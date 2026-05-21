// Per-applet test: Stairs.
//
// Manifest: shim/include/applet_manifests/Stairs.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Stairs.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on a
//   gate input asserts HS::frame.clocked[ch] = true for ALL 10 inner
//   Controller() calls. Stairs increments curr_step inside if (Clock(0)),
//   so one bus-level rising edge fires the increment 10 times.
//
//   Coverage shape: MODEL-THE-MULTIPLIER (shape 1). The clock-advance test
//   mirrors ST3 from test_hemispheres.cpp: steps=3, dir=0, one Clock(0)
//   buffer fires 10 increments, wraps twice (period=4), final curr_step=2,
//   cv_out = Proportion(2,3,9216) = 6144 hem = 4.0V.
//
// Bus parameter layout (4 inputs + 2 outputs):
//   v[0]  = input 0 (Clock)    bus selector, default 1  (gate)
//   v[1]  = input 1 (Reset)    bus selector, default 2  (gate)
//   v[2]  = input 2 (Steps CV) bus selector, default 3  (cv)
//   v[3]  = input 3 (Pos CV)   bus selector, default 4  (cv)
//   v[4]  = output 0 (Step CV) bus selector, default 13 (cv)
//   v[5]  = output 0 mode,     default 1 (replace)
//   v[6]  = output 1 (BoC Trg) bus selector, default 14 (gate)
//   v[7]  = output 1 mode,     default 1 (replace)
//
// Vendor pack layout (OnDataRequest):
//   bits [0,5)  = steps (0..31, unbiased)
//   bits [5,2)  = dir   (0=up, 1=up-down, 2=down)
//   bits [7,1)  = rand  (0=off, 1=on)
//
// After Start(): steps=1, dir=0, rand=0, curr_step=0.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Test seams defined in plugins/applets/Stairs.cpp.
uint64_t stairs_applet_on_data_request(_NT_algorithm* self);
void     stairs_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

namespace {

constexpr int kClockBusIdx   = 1;   // v[0] default - Clock gate input
constexpr int kResetBusIdx   = 2;   // v[1] default - Reset gate input
constexpr int kStepCVBusIdx  = 3;   // v[2] default - Steps CV input
constexpr int kPosCVBusIdx   = 4;   // v[3] default - Position CV input
constexpr int kStepOutBusIdx = 13;  // v[4] default - Step CV output
constexpr int kBoCBusIdx     = 14;  // v[6] default - BoC Trigger gate output

constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

// 1536 hem units per volt (matches shim copy_bus_to_frame).
constexpr float kHemPerVolt = 1536.0f;

void clear_buses(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a constant CV value (in volts) across all frames of a 1-based bus.
void write_cv_bus(float* bus, int bus_1based, float volts) {
    float* dst = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) dst[i] = volts;
}

// Write a single-sample rising-edge gate pulse at frame 0 on a 1-based bus.
void pulse_gate_bus(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    slice[0] = 6.0f;
}

// Write a full-buffer gate high (all frames) on a 1-based bus.
void hold_gate_bus(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) slice[i] = 6.0f;
}

// Returns the value on the last frame of a 1-based bus.
float read_bus_last(const float* bus, int bus_1based) {
    return bus[(bus_1based - 1) * kNumFrames + (kNumFrames - 1)];
}

// Returns true if any frame on the given 1-based bus exceeds gate threshold.
bool any_gate_high(const float* bus, int bus_1based) {
    const float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) {
        if (slice[i] > 0.5f) return true;
    }
    return false;
}

// Pack Stairs state to match vendor OnDataRequest encoding.
// steps: [0,5), unbiased. dir: [5,2). rand: [7,1).
uint64_t pack_stairs(int steps, int dir, int rand_on) {
    uint64_t d = 0;
    d |= (uint64_t)(steps   & 0x1F) << 0;
    d |= (uint64_t)(dir     & 0x03) << 5;
    d |= (uint64_t)(rand_on & 0x01) << 7;
    return d;
}

struct Setup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
};

Setup make_setup() {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus = nt::bus_frames_base();
    clear_buses(bus);
    return { loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// ST1: Start() defaults match vendor (steps=1, dir=0, rand=0).
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST1: OnDataRequest default state after Start",
          "[per-applet][stairs]") {
    auto s = make_setup();

    uint64_t packed = stairs_applet_on_data_request(s.alg);
    REQUIRE((packed & 0x1F) == 1u);       // steps = 1
    REQUIRE(((packed >> 5) & 0x03) == 0u); // dir = 0 (up)
    REQUIRE(((packed >> 7) & 0x01) == 0u); // rand = 0
}

// ---------------------------------------------------------------------------
// ST2: OnDataReceive then OnDataRequest round-trip preserves all fields.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST2: serialise round-trip preserves steps=8, dir=2, rand=1",
          "[per-applet][stairs]") {
    auto s = make_setup();

    uint64_t state_in = pack_stairs(8, 2, 1);
    stairs_applet_on_data_receive(s.alg, state_in);

    uint64_t packed = stairs_applet_on_data_request(s.alg);
    REQUIRE((packed & 0x1F) == 8u);
    REQUIRE(((packed >> 5) & 0x03) == 2u);
    REQUIRE(((packed >> 7) & 0x01) == 1u);
}

// ---------------------------------------------------------------------------
// ST3: Clock(0) up-mode advances curr_step 10x per buffer, wraps correctly.
//
// Mirrors test_hemispheres.cpp "stairs ST3" (model-the-multiplier shape):
//   steps=3, dir=0. ticks_this_step=10. wrap period = steps+1 = 4.
//   10 increments from curr_step=0:
//     ticks 1-3: curr_step=1,2,3.
//     tick  4:   ++curr_step=4 > 3 -> curr_step=0 (wrap + ClockOut(1)).
//     ticks 5-7: curr_step=1,2,3.
//     tick  8:   ++curr_step=4 > 3 -> curr_step=0 (wrap + ClockOut(1)).
//     ticks 9-10: curr_step=1,2.
//   Final curr_step=2. cv_out=Proportion(2,3,9216)=6144 hem = 4.0V.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST3: Clock(0) up-mode 10x multiplier: steps=3 yields 4.0V after one buffer",
          "[per-applet][stairs]") {
    auto s = make_setup();

    stairs_applet_on_data_receive(s.alg, pack_stairs(3, 0, 0));
    clear_buses(s.bus);

    pulse_gate_bus(s.bus, kClockBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float cv_out = read_bus_last(s.bus, kStepOutBusIdx);
    REQUIRE(cv_out == Catch::Approx(4.0f).margin(0.1f));
}

// ---------------------------------------------------------------------------
// ST4: BoC Trigger (ClockOut(1)) fires when curr_step wraps in up-mode.
//
// Same setup as ST3: two wraps occur at ticks 4 and 8. ClockOut(1) sets
// clock_countdown; with 10 ticks remaining the output stays high.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST4: BoC Trigger goes high when curr_step wraps in up-mode",
          "[per-applet][stairs]") {
    auto s = make_setup();

    stairs_applet_on_data_receive(s.alg, pack_stairs(3, 0, 0));
    clear_buses(s.bus);

    pulse_gate_bus(s.bus, kClockBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kBoCBusIdx));
}

// ---------------------------------------------------------------------------
// ST5: Clock(0) up-down mode (steps=4, dir=1).
//
// Mirrors test_hemispheres.cpp "stairs ST5":
//   10 ticks from curr_step=0:
//     ticks 1-4: curr_step=1,2,3,4.
//     tick  5:   ++curr_step=5 > 4 -> reverse=true, curr_step=3.
//     ticks 6-7: curr_step=2,1.
//     tick  8:   --curr_step=0 -> ClockOut(1). Stay reverse.
//     tick  9:   --curr_step=-1 < 0 -> reverse=false, curr_step=1.
//     tick 10:   ++curr_step=2.
//   Final curr_step=2. cv_out=Proportion(2,4,9216)=4608 hem = 3.0V.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST5: up-down mode (steps=4, dir=1) yields 3.0V after one clock buffer",
          "[per-applet][stairs]") {
    auto s = make_setup();

    stairs_applet_on_data_receive(s.alg, pack_stairs(4, 1, 0));
    clear_buses(s.bus);

    pulse_gate_bus(s.bus, kClockBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float cv_out = read_bus_last(s.bus, kStepOutBusIdx);
    REQUIRE(cv_out == Catch::Approx(3.0f).margin(0.1f));
}

// ---------------------------------------------------------------------------
// ST6: Reset gate (Clock(1)) resets curr_step to 0 and fires BoC.
//
// Set steps=4, dir=0, advance one Clock(0) buffer to get curr_step > 0.
// Then fire Reset gate. Clock(1) in Controller calls Reset() which sets
// curr_step=0, then calls ClockOut(1). With 10 ticks all firing Reset,
// curr_step stays 0 and BoC fires. cv_out = 0V.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST6: Reset gate returns curr_step to 0 and fires BoC",
          "[per-applet][stairs]") {
    auto s = make_setup();

    stairs_applet_on_data_receive(s.alg, pack_stairs(4, 0, 0));

    // Advance past step 0 with a clock buffer.
    clear_buses(s.bus);
    pulse_gate_bus(s.bus, kClockBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // Now fire Reset. Clear gate_prev by clearing bus then sending reset pulse.
    clear_buses(s.bus);
    pulse_gate_bus(s.bus, kResetBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    float cv_out = read_bus_last(s.bus, kStepOutBusIdx);
    REQUIRE(cv_out == Catch::Approx(0.0f).margin(0.01f));
    REQUIRE(any_gate_high(s.bus, kBoCBusIdx));
}

// ---------------------------------------------------------------------------
// ST7: Steps CV input overrides knob-based step count.
//
// With Steps CV = 3V -> ProportionCV(In(0), 32) maps 3V (=4608 hem) to
// approximately (4608 * 32 / 9216) = 16 steps. DetentedIn(0) > 0 so the
// steps override is active. Drive one clock buffer (steps=16): 10 increments
// from step 0, period=17. Final curr_step = 10 (no wraps).
// cv_out = Proportion(10, 16, 9216) = 5760 hem = 3.75V.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST7: Steps CV input controls step count (steps CV = 3V -> ~16 steps)",
          "[per-applet][stairs]") {
    auto s = make_setup();

    // Start with default state (steps=1 from knob, will be overridden by CV).
    clear_buses(s.bus);
    write_cv_bus(s.bus, kStepCVBusIdx, 3.0f);   // DetentedIn(0) > 0 -> override steps
    pulse_gate_bus(s.bus, kClockBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With steps=16 (16 positions 0..15 plus endpoint 16), 10 increments from 0
    // gives curr_step=10, no wraps (period = steps+1 = 17 > 10).
    // cv_out = Proportion(10, 16, 9216) = 5760 hem = 3.75V.
    float cv_out = read_bus_last(s.bus, kStepOutBusIdx);
    REQUIRE(cv_out == Catch::Approx(3.75f).margin(0.2f));
}

// ---------------------------------------------------------------------------
// ST8: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST8: hasCustomUi returns expected bitmask",
          "[per-applet][stairs]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL | kNT_button1));
}

// ---------------------------------------------------------------------------
// ST9: Encoder turn adjusts steps via customUi (no edit mode = cursor moves).
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST9: encoder turn in non-edit mode moves cursor, not value",
          "[per-applet][stairs]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t before = stairs_applet_on_data_request(loaded->algorithm);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    // Cursor move should not change serialised state.
    uint64_t after = stairs_applet_on_data_request(loaded->algorithm);
    REQUIRE(after == before);
}

// ---------------------------------------------------------------------------
// ST10: JSON serialise/deserialise round-trip.
// ---------------------------------------------------------------------------

TEST_CASE("Stairs ST10: JSON serialise then deserialise preserves packed state",
          "[per-applet][stairs]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    uint64_t state_in = pack_stairs(5, 1, 1);
    stairs_applet_on_data_receive(alg, state_in);

    uint32_t lo = (uint32_t)(stairs_applet_on_data_request(alg) & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(stairs_applet_on_data_request(alg) >> 32);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    bool ok = loaded->factory->deserialise(alg, *parse);
    REQUIRE(ok);

    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    loaded->factory->serialise(alg, *stream);
    const std::string& out = stream->buffer();

    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::atoi(colon + 1);
    // Verify steps=5 and rand=1 survived: steps in [0,5) and rand in bit [7].
    REQUIRE((rt_lo & 0x1Fu) == 5u);   // steps
    REQUIRE(((rt_lo >> 7) & 0x01u) == 1u);  // rand
}
