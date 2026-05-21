// Per-applet test: Brancher.
//
// Manifest: shim/include/applet_manifests/Brancher.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/Brancher.h
//
// 10x clocked-multiplier acknowledgement (CLAUDE.md "Critical gotcha"):
//   The per-applet runtime fires vendor Controller() 10 times per buffer
//   (ticks_this_step = numFrames/3 = 32/3 = 10). A single rising edge on a
//   gate input asserts HS::frame.clocked[ch] = true, which stays asserted
//   across all 10 inner Controller() calls. Brancher's Controller reads
//   Clock(0) and Clock(1) once per tick, so one bus-level rising edge
//   produces 10 logical clock ticks inside the applet.
//
//   Coverage shape chosen: SHAPE 2 (round-trip + state injection only).
//   Bus-level fire-count assertions are dropped. Behavioral coverage checks
//   that gate outputs go high after a clock input and that serialised state
//   survives round-trip. This avoids brittle math on the 10x multiplier.
//
// Bus parameter layout (from emit_base_parameters, 3 inputs + 2 outputs):
//   v[0]  = input 0 (Clock/Gate) bus selector, default 1
//   v[1]  = input 1 (AltClk)     bus selector, default 2
//   v[2]  = input 2 (p Mod)      bus selector, default 3
//   v[3]  = output 0 (Left) bus selector, default 13
//   v[4]  = output 0 (Left) mode, default 1 (replace)
//   v[5]  = output 1 (Right) bus selector, default 14
//   v[6]  = output 1 (Right) mode, default 1 (replace)
//
// Vendor serialisation:
//   OnDataRequest packs p (0-100) into bits [0,7) (7-bit field).
//   Start() initialises p=50.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

// Test seams defined in plugins/applets/Brancher.cpp.
uint64_t brancher_applet_on_data_request(_NT_algorithm* self);
void     brancher_applet_on_data_receive(_NT_algorithm* self, uint64_t data);

namespace {

constexpr int kClockBusIdx = 1;   // v[0] default - Clock/Gate input
constexpr int kAltClkBusIdx = 2;  // v[1] default - AltClk input
constexpr int kLeftBusIdx  = 13;  // v[3] default - Left gate output
constexpr int kRightBusIdx = 14;  // v[5] default - Right gate output
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;  // = 8

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Write a single-sample rising-edge pulse at frame 0 on the given 1-based bus.
void pulse_bus(float* bus, int bus_1based) {
    float* slice = bus + (bus_1based - 1) * kNumFrames;
    slice[0] = 6.0f;
}

// Returns true if any frame on the given 1-based bus exceeds gate threshold.
bool any_gate_high(const float* bus, int bus_1based) {
    const float* slice = bus + (bus_1based - 1) * kNumFrames;
    for (int i = 0; i < kNumFrames; ++i) {
        if (slice[i] > 0.5f) return true;
    }
    return false;
}

// Returns true if either Left or Right output bus is gate-high.
bool either_output_high(const float* bus) {
    return any_gate_high(bus, kLeftBusIdx) || any_gate_high(bus, kRightBusIdx);
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
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    // One warmup step to let BaseStart settle.
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

}  // namespace

// ---------------------------------------------------------------------------
// BR1: pluginEntry returns factory with correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR1: pluginEntry returns factory with correct guid", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    uint32_t expected = NT_MULTICHAR('H','m','B','r');
    REQUIRE(loaded->factory->guid == expected);
    REQUIRE(std::string(loaded->factory->name) == "Brancher");
}

// ---------------------------------------------------------------------------
// BR2: construct populates HemiPluginInterface fields correctly.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR2: construct populates HemiPluginInterface magic and version", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* p = static_cast<HemiPluginInterface*>(loaded->algorithm);
    REQUIRE(p->magic             == kHemiInterfaceMagic);
    REQUIRE(p->interface_version == kHemiInterfaceVersion);
    REQUIRE(p->render_view             != nullptr);
    REQUIRE(p->on_encoder_turn         != nullptr);
    REQUIRE(p->on_encoder_turn_shifted != nullptr);
    REQUIRE(p->on_button_press         != nullptr);
    REQUIRE(p->on_aux_button           != nullptr);
}

// ---------------------------------------------------------------------------
// BR3: OnDataRequest packs p=50 after Start.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR3: OnDataRequest packs p=50 after Start", "[per-applet][brancher]") {
    // Vendor Start() sets p=50. OnDataRequest packs it into bits [0,7).
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    uint64_t packed = brancher_applet_on_data_request(loaded->algorithm);
    REQUIRE((packed & 0x7F) == 50u);
}

// ---------------------------------------------------------------------------
// BR4: serialise round-trip preserves p.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR4: serialise round-trip preserves p", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Inject p=75 and confirm it round-trips.
    uint64_t state_in = 75u;  // bits [0,7) = 75
    brancher_applet_on_data_receive(alg, state_in);
    uint64_t packed = brancher_applet_on_data_request(alg);
    REQUIRE((packed & 0x7F) == 75u);
}

// ---------------------------------------------------------------------------
// BR5: serialise round-trip via JSON preserves p.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR5: serialise/deserialise JSON round-trip preserves p", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    // Inject p=80.
    brancher_applet_on_data_receive(alg, 80u);
    uint32_t lo = (uint32_t)(brancher_applet_on_data_request(alg) & 0xFFFFFFFFu);
    uint32_t hi = 0u;

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
    REQUIRE((rt_lo & 0x7Fu) == 80u);
}

// ---------------------------------------------------------------------------
// BR6: clock input routes gate to one output (shape-2: any output high).
//
// With p=100 (always route to output 0 = Left), a clock pulse on input 0
// must result in Left going high.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR6: clock on input 0 with p=100 drives Left output high", "[per-applet][brancher]") {
    auto s = make_setup();

    // Set p=100 so choice is always 0 (Left). OnDataRequest packs p in [0,7).
    brancher_applet_on_data_receive(s.alg, 100u);

    // Drive Clock/Gate input (bus 1) with a rising edge.
    pulse_bus(s.bus, kClockBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // With p=100, choice is always 0 (Left). Left output must go high.
    REQUIRE(any_gate_high(s.bus, kLeftBusIdx));
}

// ---------------------------------------------------------------------------
// BR7: clock input with p=0 drives Right output high.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR7: clock on input 0 with p=0 drives Right output high", "[per-applet][brancher]") {
    auto s = make_setup();

    // Set p=0 so choice is always 1 (Right).
    brancher_applet_on_data_receive(s.alg, 0u);

    pulse_bus(s.bus, kClockBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(any_gate_high(s.bus, kRightBusIdx));
}

// ---------------------------------------------------------------------------
// BR8: AltClk input (bus 2) also triggers output routing.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR8: AltClk input with p=100 drives Left output high", "[per-applet][brancher]") {
    auto s = make_setup();

    brancher_applet_on_data_receive(s.alg, 100u);

    // Drive AltClk input (bus 2).
    pulse_bus(s.bus, kAltClkBusIdx);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    // AltClk enables flip-flop mode; with p=100 choice is always 0 (Left).
    REQUIRE(any_gate_high(s.bus, kLeftBusIdx));
}

// ---------------------------------------------------------------------------
// BR9: hasCustomUi returns expected bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR9: hasCustomUi returns expected bitmask", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL | kNT_button1));
}

// ---------------------------------------------------------------------------
// BR10: encoder turn adjusts p via customUi.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR10: customUi encoder turn increments p", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    // Default p=50.
    REQUIRE((brancher_applet_on_data_request(loaded->algorithm) & 0x7F) == 50u);

    _NT_uiData ui{};
    ui.encoders[0] = 1;
    ui.controls    = 0;
    ui.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, ui);

    REQUIRE((brancher_applet_on_data_request(loaded->algorithm) & 0x7F) == 51u);
}

// ---------------------------------------------------------------------------
// BR11: encoder button press (OnButtonPress) does not crash.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR11: customUi encoder button press routes OnButtonPress", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_encoderButtonL;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// BR12: button1 press routes on_aux_button without crashing.
// ---------------------------------------------------------------------------

TEST_CASE("Brancher BR12: customUi button1 press routes on_aux_button", "[per-applet][brancher]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    _NT_uiData ui{};
    ui.encoders[0]  = 0;
    ui.controls     = kNT_button1;
    ui.lastButtons  = 0;
    loaded->factory->customUi(loaded->algorithm, ui);
    REQUIRE(true);
}
