// Per-applet test: hMIDIIn (MIDI -> CV).
//
// Manifest: shim/include/applet_manifests/hMIDIIn.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/hMIDIIn.h
//
// MIDI is injected through the harness seam nt::send_midi_to_plugin /
// nt::send_midi_realtime, which call the plug-in's factory midiMessage /
// midiRealtime callbacks (Layer 0c) -> HS::frame.MIDIState.ProcessMIDIMsg. The
// test configures MIDIState.mapping[] directly (the host links one shared
// HS::frame) and asserts the decoded output reaches the physical bus.
//
// 10x clocked-multiplier (CLAUDE.md "Critical gotcha"): hMIDIIn's Controller()
// reads only mapping[].output and clock_run and never calls Clock()/Gate(), so
// the inner-tick multiplier does not multiply any MIDI-driven state. The clock
// realtime path advances inside ProcessMIDIMsg (one step per injected byte),
// not inside Controller(). Coverage shape: state injection + round-trip.

#include "catch.hpp"
#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"
#include "HemiPluginInterface.h"
#include "HSIOFrame.h"
#include "HSMIDIFrame.h"
#include "HSMIDI.h"
#include "quant/MIDIQuantizer.h"
#include <distingnt/api.h>
#include <cstring>
#include <string>

namespace {

// hMIDIIn manifest base-parameter layout (emit_base_parameters):
//   v[0] = input 0 bus (unused), v[1] = input 1 bus (unused)
//   v[2] = output 0 (Out A) bus, default 13
//   v[3] = output 0 mode, default 1 (replace)
//   v[4] = output 1 (Out B) bus, default 14
//   v[5] = output 1 mode, default 1
constexpr int kOutABusIdx   = 13;
constexpr int kOutBBusIdx   = 14;
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Mean of a 1-based bus's frames (the runtime writes a constant across frames).
float bus_value(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    return slice[0];
}

bool any_high(const float* bus, int bus_1based, int numFrames) {
    const float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i)
        if (slice[i] > 0.5f) return true;
    return false;
}

struct Setup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
};

Setup make_setup() {
    nt::reset_runtime();
    // HS::frame.MIDIState is a process-global shared across Catch test cases;
    // nt::reset_runtime() does not touch it. Re-seed defaults and clear all
    // note/poly/sustain state so each test starts from a known MIDI state.
    HS::frame.MIDIState.Init();
    HS::frame.MIDIState.ClearMonoBuffer();
    HS::frame.MIDIState.ClearPolyBuffer();
    HS::frame.MIDIState.ClearSustainLatch();
    HS::frame.MIDIState.clock_run = false;
    HS::frame.MIDIState.clock_count = 0;
    HS::frame.MIDIState.log_index = 0;
    HS::frame.MIDIState.UpdateMidiChannelFilter();
    HS::frame.MIDIState.UpdateMaxPolyphony();

    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    return Setup{ loaded, loaded->algorithm, bus };
}

// MIDI status helpers.
constexpr uint8_t kNoteOn  = 0x90;
constexpr uint8_t kNoteOff = 0x80;

}  // namespace

// ---------------------------------------------------------------------------
// MI1: factory has the expected guid, name, and the MIDI receive callbacks.
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIIn MI1: factory guid/name and midi callbacks present", "[per-applet-pilot][hmidiin]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->factory->guid == NT_MULTICHAR('H','m','M','i'));
    REQUIRE(std::string(loaded->factory->name) == "MIDI In");
    // hMIDIIn is a MIDI consumer: both callbacks must be wired.
    REQUIRE(loaded->factory->midiMessage  != nullptr);
    REQUIRE(loaded->factory->midiRealtime != nullptr);
}

// ---------------------------------------------------------------------------
// MI2: injected NoteOn produces the quantizer CV on the mapped output.
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIIn MI2: NoteOn maps to MIDIQuantizer::CV on Out A", "[per-applet-pilot][hmidiin]") {
    auto s = make_setup();

    // Configure mapping[0] (Out A) as a NOTE_OUT on channel 0.
    auto& midi = HS::frame.MIDIState;
    midi.mapping[0].function = HEM_MIDI_NOTE_OUT;
    midi.mapping[0].channel  = 0;
    midi.mapping[0].range_low = 0;
    midi.mapping[0].range_high = 127;
    midi.UpdateMidiChannelFilter();

    const uint8_t note = 60; // middle C
    nt::send_midi_to_plugin(s.loaded, (uint8_t)(kNoteOn | 0), note, 100);

    // The mapping output is now CV(note); step copies it to Out A.
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    int expected_hem = MIDIQuantizer::CV(note);
    float expected_v = (float)expected_hem / 1536.0f;
    float got = bus_value(s.bus, kOutABusIdx, kNumFrames);
    REQUIRE(got == Catch::Approx(expected_v).margin(0.01f));
    REQUIRE(midi.mapping[0].output == expected_hem);
}

// ---------------------------------------------------------------------------
// MI3: NoteOff drops the gate output back to 0 (GATE_OUT mapping).
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIIn MI3: NoteOff releases a gate-mapped output", "[per-applet-pilot][hmidiin]") {
    auto s = make_setup();

    auto& midi = HS::frame.MIDIState;
    midi.mapping[0].function = HEM_MIDI_GATE_OUT;
    midi.mapping[0].channel  = 0;
    midi.mapping[0].range_low = 0;
    midi.mapping[0].range_high = 127;
    midi.UpdateMidiChannelFilter();

    const uint8_t note = 64;
    nt::send_midi_to_plugin(s.loaded, (uint8_t)(kNoteOn | 0), note, 100);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    REQUIRE(any_high(s.bus, kOutABusIdx, kNumFrames));  // gate high while held

    clear_bus(s.bus);
    nt::send_midi_to_plugin(s.loaded, (uint8_t)(kNoteOff | 0), note, 0);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    REQUIRE(midi.mapping[0].output == 0);                 // released
    REQUIRE_FALSE(any_high(s.bus, kOutABusIdx, kNumFrames));
}

// ---------------------------------------------------------------------------
// MI4: realtime Start/Clock drives clock_run and the RUN_OUT gate output.
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIIn MI4: realtime clock/start drives clock_run and clock-out", "[per-applet-pilot][hmidiin]") {
    auto s = make_setup();

    auto& midi = HS::frame.MIDIState;
    // Out B mapped to RUN_OUT: hMIDIIn Controller GateOuts clock_run there.
    midi.mapping[1].function = HEM_MIDI_RUN_OUT;
    midi.mapping[1].channel  = 0;
    midi.UpdateMidiChannelFilter();

    // Before start, clock_run is false.
    REQUIRE(midi.clock_run == false);

    // MIDI Start (0xFA) sets clock_run true.
    nt::send_midi_realtime(s.loaded, HS::HEM_MIDI_START);
    REQUIRE(midi.clock_run == true);

    clear_bus(s.bus);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
    REQUIRE(any_high(s.bus, kOutBBusIdx, kNumFrames));  // RUN_OUT gate follows clock_run

    // MIDI clock (0xF8) advances the 24ppqn counter.
    uint8_t before = midi.clock_count;
    nt::send_midi_realtime(s.loaded, HS::HEM_MIDI_CLOCK);
    REQUIRE(midi.clock_count == (uint8_t)(before + 1));

    // MIDI Stop (0xFC) clears clock_run.
    nt::send_midi_realtime(s.loaded, HS::HEM_MIDI_STOP);
    REQUIRE(midi.clock_run == false);
}

// ---------------------------------------------------------------------------
// MI5: serialise/deserialise round-trips the packed map_index fields.
//
// hMIDIIn OnDataRequest packs map_index[0]@[0,5) and map_index[1]@[8,5).
// ---------------------------------------------------------------------------

static uint64_t local_pack_hmidiin(int map0, int map1) {
    uint64_t data = 0;
    data |= (uint64_t)(map0 & 0x1F);
    data |= (uint64_t)(map1 & 0x1F) << 8;
    return data;
}

TEST_CASE("hMIDIIn MI5: serialise round-trip preserves map_index", "[per-applet-pilot][hmidiin]") {
    uint64_t packed = local_pack_hmidiin(3, 5);
    REQUIRE((int)(packed & 0x1F) == 3);
    REQUIRE((int)((packed >> 8) & 0x1F) == 5);

    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[128];
    std::snprintf(json_buf, sizeof(json_buf),
        R"({"hemi_hi":%u,"hemi_lo":%u})", (unsigned)hi, (unsigned)lo);

    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;

    auto parse = nt::make_json_parse(std::string(json_buf));
    REQUIRE(parse != nullptr);
    REQUIRE(loaded->factory->deserialise(alg, *parse));

    auto stream = nt::make_json_stream();
    REQUIRE(stream != nullptr);
    loaded->factory->serialise(alg, *stream);
    const std::string& out = stream->buffer();

    const char* lo_pos = std::strstr(out.c_str(), "hemi_lo");
    REQUIRE(lo_pos != nullptr);
    const char* colon = std::strchr(lo_pos, ':');
    REQUIRE(colon != nullptr);
    uint32_t rt_lo = (uint32_t)std::strtoul(colon + 1, nullptr, 10);
    REQUIRE(rt_lo == lo);
}

// ---------------------------------------------------------------------------
// MI6: hasCustomUi returns the expected encoder bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIIn MI6: hasCustomUi returns encoder bitmask", "[per-applet-pilot][hmidiin]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}
