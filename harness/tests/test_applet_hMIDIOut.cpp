// Per-applet test: hMIDIOut (CV -> MIDI).
//
// Manifest: shim/include/applet_manifests/hMIDIOut.h
// Vendor:   vendor/O_C-Phazerville/software/src/applets/hMIDIOut.h
//
// hMIDIOut reads Gate(0) plus In(0) (pitch) and In(1) (second function) and
// emits MIDI through HS::frame.MIDIState.Send* (Layer 0b). On the host the NT
// MIDI sends are captured by the harness; tests read them via nt::midi_sent().
//
// 10x clocked-multiplier (CLAUDE.md "Critical gotcha"): hMIDIOut uses the
// ADC-lag handshake (StartADCLag on the gate rising edge, EndOfADCLag counting
// down) to fire one NoteOn per gate edge. HEMISPHERE_ADC_LAG = 96 and the
// runtime runs Controller() ~10 times per buffer, so the lag spans ~10 steps.
// The gate is held HIGH across those steps (not re-pulsed), so the rising-edge
// guard fires once. NoteOff is sent when the gate is released.

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

// hMIDIOut manifest base-parameter layout (emit_base_parameters):
//   v[0] = input 0 (Gate)  bus, default 1
//   v[1] = input 1 (Pitch) bus, default 2
//   v[2] = input 2 (Func)  bus, default 3
//   v[3] = output 0 (Unused) bus, default 13
//   v[4] = output 0 mode, default 1
constexpr int kGateBusIdx  = 1;
constexpr int kPitchBusIdx = 2;
constexpr int kNumFrames    = 32;
constexpr int kNumFramesBy4 = kNumFrames / 4;

void clear_bus(float* bus) {
    std::memset(bus, 0, sizeof(float) * nt::num_buses() * nt::bus_frame_count());
}

// Hold a bus at a constant value across all frames (a level / held gate).
void hold_bus(float* bus, int bus_1based, int numFrames, float value) {
    float* slice = bus + (bus_1based - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = value;
}

struct Setup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
};

Setup make_setup() {
    nt::reset_runtime();
    // HS::frame.MIDIState is process-global and shared across Catch test cases;
    // re-seed defaults so each test starts clean.
    HS::frame.MIDIState.Init();
    HS::frame.MIDIState.ClearMonoBuffer();
    HS::frame.MIDIState.ClearPolyBuffer();
    HS::frame.MIDIState.ClearSustainLatch();

    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    loaded->factory->step(loaded->algorithm, bus, kNumFramesBy4);
    clear_bus(bus);
    nt::clear_midi_sent();
    return Setup{ loaded, loaded->algorithm, bus };
}

// Find the first captured NoteOn (status 0x90|ch). Returns index or -1.
int find_note_on() {
    const auto& sent = nt::midi_sent();
    for (size_t i = 0; i < sent.size(); ++i) {
        if (sent[i].length == 3 && (sent[i].bytes[0] & 0xF0) == 0x90 && sent[i].bytes[2] > 0)
            return (int)i;
    }
    return -1;
}

int find_note_off() {
    const auto& sent = nt::midi_sent();
    for (size_t i = 0; i < sent.size(); ++i) {
        // NoteOff is either 0x80 or a 0x90 with velocity 0.
        if (sent[i].length == 3 &&
            ((sent[i].bytes[0] & 0xF0) == 0x80 ||
             ((sent[i].bytes[0] & 0xF0) == 0x90 && sent[i].bytes[2] == 0)))
            return (int)i;
    }
    return -1;
}

}  // namespace

// ---------------------------------------------------------------------------
// MO1: factory guid/name; hMIDIOut is a MIDI producer so it has NO receive
// callbacks.
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIOut MO1: factory guid/name; no midi receive callbacks", "[per-applet-pilot][hmidiout]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->factory->guid == NT_MULTICHAR('H','m','M','o'));
    REQUIRE(std::string(loaded->factory->name) == "MIDI Out");
    REQUIRE(loaded->factory->midiMessage  == nullptr);
    REQUIRE(loaded->factory->midiRealtime == nullptr);
}

// ---------------------------------------------------------------------------
// MO2: Gate + Pitch CV emit a NoteOn(channel, MIDIQuantizer::NoteNumber(cv)).
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIOut MO2: gate + pitch emit NoteOn with quantized note", "[per-applet-pilot][hmidiout]") {
    auto s = make_setup();

    // Pitch = +1.000 V -> one octave above the zero-bias note. Hold pitch and
    // gate high across enough steps for the ADC lag (96 ticks at ~10/step) to
    // expire and fire a single NoteOn.
    const float pitch_v = 1.0f;
    int expected_note = MIDIQuantizer::NoteNumber((int)(pitch_v * 1536.0f));

    int note_on_idx = -1;
    for (int step = 0; step < 20 && note_on_idx < 0; ++step) {
        clear_bus(s.bus);
        hold_bus(s.bus, kGateBusIdx, kNumFrames, 6.0f);   // gate high
        hold_bus(s.bus, kPitchBusIdx, kNumFrames, pitch_v);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
        note_on_idx = find_note_on();
    }

    REQUIRE(note_on_idx >= 0);
    const auto& sent = nt::midi_sent();
    REQUIRE((sent[note_on_idx].bytes[0] & 0x0F) == 0); // default channel 0 (MIDI ch 1)
    REQUIRE(sent[note_on_idx].bytes[1] == expected_note);
    REQUIRE(sent[note_on_idx].bytes[2] == 0x64);       // default velocity 100
}

// ---------------------------------------------------------------------------
// MO3: releasing the gate sends a NoteOff.
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIOut MO3: releasing gate sends NoteOff", "[per-applet-pilot][hmidiout]") {
    auto s = make_setup();

    const float pitch_v = 1.0f;
    // Drive gate high until the NoteOn fires.
    bool fired = false;
    for (int step = 0; step < 20 && !fired; ++step) {
        clear_bus(s.bus);
        hold_bus(s.bus, kGateBusIdx, kNumFrames, 6.0f);
        hold_bus(s.bus, kPitchBusIdx, kNumFrames, pitch_v);
        s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);
        fired = (find_note_on() >= 0);
    }
    REQUIRE(fired);

    nt::clear_midi_sent();

    // Release the gate (bus low) while keeping pitch. A NoteOff should follow.
    clear_bus(s.bus);
    hold_bus(s.bus, kPitchBusIdx, kNumFrames, pitch_v);
    s.loaded->factory->step(s.alg, s.bus, kNumFramesBy4);

    REQUIRE(find_note_off() >= 0);
}

// ---------------------------------------------------------------------------
// MO4: serialise/deserialise round-trips the packed settings.
//
// hMIDIOut OnDataRequest packs:
//   channel@[0,4), legato@[7,1), functionA@[8,8), functionB@[16,8),
//   transpose@[24,8).
// ---------------------------------------------------------------------------

static uint64_t local_pack_hmidiout(int channel, int legato, int functionA,
                                    int functionB, int transpose) {
    uint64_t data = 0;
    data |= (uint64_t)(channel   & 0x0F);
    data |= (uint64_t)(legato    & 0x01) << 7;
    data |= (uint64_t)(functionA & 0xFF) << 8;
    data |= (uint64_t)(functionB & 0xFF) << 16;
    data |= (uint64_t)((uint8_t)transpose & 0xFF) << 24;
    return data;
}

TEST_CASE("hMIDIOut MO4: serialise round-trip preserves settings", "[per-applet-pilot][hmidiout]") {
    // channel 5, legato 1, functionA = NOTE (0), functionB = CC_CONTROL+1,
    // transpose +7.
    const int functionB = 4 + 1; // CC_CONTROL == 4 in MIDIOutMode
    uint64_t packed = local_pack_hmidiout(5, 1, 0, functionB, 7);
    REQUIRE((int)(packed & 0x0F) == 5);
    REQUIRE((int)((packed >> 7) & 0x01) == 1);
    REQUIRE((int)((packed >> 8) & 0xFF) == 0);
    REQUIRE((int)((packed >> 16) & 0xFF) == functionB);
    REQUIRE((int8_t)((packed >> 24) & 0xFF) == 7);

    uint32_t hi = (uint32_t)(packed >> 32);
    uint32_t lo = (uint32_t)(packed & 0xFFFFFFFFu);

    char json_buf[160];
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
// MO5: hasCustomUi returns the expected encoder bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("hMIDIOut MO5: hasCustomUi returns encoder bitmask", "[per-applet-pilot][hmidiout]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE(mask == (kNT_encoderL | kNT_encoderButtonL));
}
