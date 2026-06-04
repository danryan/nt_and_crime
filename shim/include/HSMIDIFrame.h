// HSMIDIFrame.h -- shim-owned port of the vendor HS::MIDIFrame subsystem.
//
// The vendor MIDIFrame lives in vendor/O_C-Phazerville/software/src/HSIOFrame.h
// (the MIDIMessage / MIDINoteData / MIDIMapSettings / MIDIMapping /
// PolyphonyData types plus the MIDIFrame struct) with method bodies in
// HSIOFrame.cpp. That vendor IOFrame is Teensy-coupled (std::vector note
// buffers, usbMIDI sends, Teensy DAC paths) and cannot compile against the NT
// shim. This header is a faithful re-port of just the MIDI half, shim-owned so
// it MAY adapt for the NT target. Two adaptations versus vendor:
//
//   1. NoteBuffer is a fixed-capacity (16) inline container, not
//      std::vector<MIDINoteData>. No heap on the MIDI path.
//   2. The Send* methods route to NT_sendMidi2ByteMessage /
//      NT_sendMidi3ByteMessage on ARM and to harness-recorded stubs on host
//      (see shim/src/midi_frame.cpp, Layer 0b). They no longer call usbMIDI.
//
// ProcessMIDIMsg and Send bodies are ported verbatim from HSIOFrame.cpp into
// shim/src/midi_frame.cpp.
#pragma once

#include <cstdint>
#include <cstring>
#include "Arduino.h"     // constrain, min, max
#include "OC_core.h"      // OC::CORE::ticks
#include "HSMIDI.h"      // HS::HemMidiType, MIDIFunctions, MIDIPolyMode enums
#include "HSUtils.h"      // HEMISPHERE_* constants, trig_length, Pack/Unpack
#include "OC_DAC.h"       // DAC_CHANNEL_COUNT
#include "OC_ADC.h"       // ADC_CHANNEL_COUNT
#include "quant/MIDIQuantizer.h"
#include "util/util_math.h"  // SlewedValue, Proportion
#include "util/util_macros.h"  // DISALLOW_COPY_AND_ASSIGN, CONSTRAIN

// Vendor PackingUtils.h is header-only and portable (pure templates); the
// MIDIMapping::Pack/Unpack methods use PackPackables/UnpackPackables verbatim.
// Include it by BARE name (resolved through -Ivendor/.../src) so its
// `#pragma once` deduplicates against vendor applets that also include it by
// bare name (e.g. Combin8.h). A relative ../../vendor path spelling here would
// be treated as a different file and double-define the templates.
#include <tuple>
#include <type_traits>
#include "PackingUtils.h"

namespace HS {

// Non-Teensy path uses 8 map slots (matches vendor #else branch).
static constexpr int MIDIMAP_MAX = 8;

// Vendor HSUtils.h:25 defines this for the output-side change detector.
#ifndef HEMISPHERE_CHANGE_THRESHOLD
#define HEMISPHERE_CHANGE_THRESHOLD 32
#endif

struct MIDIMessage {
  // values expected from MIDI library, so channel starts at 1 (one), not zero
  uint8_t channel, message, data1, data2;

  const uint8_t chan() const { return channel - 1; }
  const uint8_t note() const { return data1; }
  const uint8_t vel() const { return data2; }
  const bool IsNote() const { return message == HEM_MIDI_NOTE_ON; }
};

using MIDILogEntry = MIDIMessage;

struct MIDINoteData {
    uint8_t note; // data1
    uint8_t vel;  // data2
};

struct PolyphonyData {
    uint8_t note;
    uint8_t vel;
    bool gate;
};

struct MIDIMapSettings {
  int8_t function_cc; // CC#, or some secret parameter for non-CC functions ;)
  uint8_t function; // which type of message
  uint8_t channel; // MIDI channel number
  uint8_t dac_polyvoice; // select which voice to send from output
  int8_t transpose;
  uint8_t range_low, range_high;
};

struct MIDIMapping : public MIDIMapSettings {
  MIDIMapping() {}
  ~MIDIMapping() {}

  static constexpr size_t Size = 64; // Make this compatible with Packable

  // state
  int16_t trigout_countdown;
  uint16_t semitone_mask; // which notes are currently on
  int16_t output; // translated CV values

  const bool IsClock() const {
    return (function >= HEM_MIDI_CLOCK_OUT);
  }
  const bool IsTrigger() const {
    return (function == HEM_MIDI_TRIG_OUT
         || function == HEM_MIDI_TRIG_1ST_OUT
         || function == HEM_MIDI_TRIG_ALWAYS_OUT
         || function == HEM_MIDI_START_OUT
         || IsClock());
  }
  constexpr int clock_mod() const {
    uint8_t mod = 1;
    if (function == HEM_MIDI_CLOCK_OUT) mod = 12;
    if (function == HEM_MIDI_CLOCK_8_OUT) mod = 6;
    if (function == HEM_MIDI_CLOCK_16_OUT) mod = 3;
    return mod;
  }
  void ClockOut() {
    trigout_countdown = HEMISPHERE_CLOCK_TICKS * HS::trig_length;
    output = HEMISPHERE_MAX_CV;
  }
  void ProcessClock(int count) {
    if ( IsClock() && ((count-1) % clock_mod() == 0) )
      ClockOut();
  }
  const bool InRange(uint8_t note) const {
    return (note >= range_low && note <= range_high);
  }

  void AdjustChannel(int dir) {
    channel = constrain(channel + dir, 0, 16);
  }
  void AdjustFunction(int dir) {
    function = constrain(function + dir, 0, HEM_MIDI_MAX_FUNCTION);
    if (function == HEM_MIDI_CC_OUT)
      function_cc = -1; // auto-learn MIDI CC
  }

  void AdjustVoice(int dir) {
    dac_polyvoice = constrain(dac_polyvoice + dir, 0, DAC_CHANNEL_COUNT - 1);
  }

  void AutoLearn() {
    channel = 16; // omni
    function = HEM_MIDI_LEARN;
    function_cc = -1; // auto-learn MIDI CC or precise NoteOn
  }

  void AdjustTranspose(int dir) {
    transpose = constrain(transpose + dir, -48, 48);
  }
  void AdjustRangeLow(int dir) {
    range_low = constrain(range_low + dir, 0, range_high);
  }
  void AdjustRangeHigh(int dir) {
    range_high = constrain(range_high + dir, range_low, 127);
  }
  uint64_t Pack() const {
    return PackPackables(function_cc, function, channel, dac_polyvoice, transpose, range_low, range_high);
  }
  void Unpack(uint64_t data) {
    UnpackPackables(data, function_cc, function, channel, dac_polyvoice, transpose, range_low, range_high);
    // validation for safety
    if (function > HEM_MIDI_MAX_FUNCTION) function = HEM_MIDI_NOOP;
    channel &= 0x1F;
    dac_polyvoice &= 0x0F;
    if (range_low == 0 && range_high == 0) range_high = 127;
    if (range_high < range_low) range_high = range_low;
  }

  DISALLOW_COPY_AND_ASSIGN(MIDIMapping);
};

// Lets PackingUtils know this is Packable as is.
constexpr MIDIMapping& pack(MIDIMapping& input) {
  return input;
}

// ADAPTATION: fixed-capacity inline replacement for
// `using NoteBuffer = std::vector<MIDINoteData>`. Exposes exactly the subset
// the MIDIFrame body uses: default ctor, size(), operator[], push_back, clear,
// begin/end, front/back, plus erase-by-value (RemoveNoteData). NO heap.
static constexpr int kNoteBufferCap = 16;

struct NoteBuffer {
    MIDINoteData data_[kNoteBufferCap];
    int size_ = 0;

    int size() const { return size_; }
    MIDINoteData& operator[](int i) { return data_[i]; }
    const MIDINoteData& operator[](int i) const { return data_[i]; }

    void push_back(const MIDINoteData& d) {
        if (size_ < kNoteBufferCap) data_[size_++] = d;
        else { // full: drop oldest, keep newest (mirror vector semantics safely)
            for (int i = 1; i < kNoteBufferCap; ++i) data_[i - 1] = data_[i];
            data_[kNoteBufferCap - 1] = d;
        }
    }
    void clear() { size_ = 0; }
    void shrink_to_fit() {} // no-op (no heap to free)

    MIDINoteData* begin() { return data_; }
    MIDINoteData* end()   { return data_ + size_; }
    const MIDINoteData* begin() const { return data_; }
    const MIDINoteData* end()   const { return data_ + size_; }

    const MIDINoteData& front() const { return data_[0]; }
    const MIDINoteData& back()  const { return data_[size_ - 1]; }

    // erase-by-value: remove all entries whose note == note.
    void erase_note(uint8_t note) {
        int w = 0;
        for (int r = 0; r < size_; ++r) {
            if (data_[r].note != note) data_[w++] = data_[r];
        }
        size_ = w;
    }
};

struct MIDIFrame {
    MIDIMapping mapping[MIDIMAP_MAX];
    MIDIMapping outmap[ADC_CHANNEL_COUNT];

    // MIDI input stuff handled by MIDIIn applet
    NoteBuffer note_buffer[16]; // note buffer to track all held notes on all channels
    uint8_t last_midi_channel = 0; // for MIDI In activity monitor
    uint16_t sustain_latch; // each bit is a MIDI channel's sustain state

    uint8_t pc_channel = 0; // program change channel filter, used for preset selection
    static constexpr uint8_t PC_OMNI = 0;

    PolyphonyData poly_buffer[DAC_CHANNEL_COUNT]; // buffer for polyphonic data tracking
    uint8_t max_voice = 1;
    int poly_mode = 0;
    int8_t poly_rotate_index = -1;
    uint16_t midi_channel_filter = 0; // each bit state represents a channel. 1 means enabled. all 0's means Omni (no channel filter)
    bool any_channel_omni = false;

    // Clock/Start/Stop are handled by ClockSetup applet
    bool clock_run = 0;
    bool clock_q;
    bool start_q;
    bool stop_q;
    uint8_t clock_count; // MIDI clock counter (24ppqn)
    uint32_t last_msg_tick; // Tick of last received message

    void Init() {
      // TODO: populate with some sensible defaults
      for (int ch = 0; ch < MIDIMAP_MAX; ++ch) {
        mapping[ch].function = HEM_MIDI_NOOP;
        mapping[ch].transpose = 0;
        mapping[ch].output = 0;
        mapping[ch].dac_polyvoice = ch / 2 % DAC_CHANNEL_COUNT; // each quad is a unique voice
        mapping[ch].range_low = 0;
        mapping[ch].range_high = 127;
      }
      for (int ch = 0; ch < ADC_CHANNEL_COUNT; ++ch) {
        outmap[ch].function = (ch & 1) ? HEM_MIDI_GATE_OUT : HEM_MIDI_NOTE_OUT;
        outmap[ch].function_cc = ch + 1;
        outmap[ch].transpose = 0;
        outmap[ch].output = 0;
        outmap[ch].range_low = 0;
        outmap[ch].range_high = 127;
      }
      clock_count = 0;
    }

    // getters for access to mappings
    uint8_t get_in_assign(int ch) {
      return mapping[ch].function;
    }
    uint8_t get_in_channel(int ch) {
      return mapping[ch].channel;
    }
    int8_t get_in_transpose(int ch) {
      return mapping[ch].transpose;
    }
    bool in_in_range(int ch, uint8_t note) {
      return mapping[ch].InRange(note);
    }

    uint8_t get_out_assign(int ch) {
      return outmap[ch].function;
    }
    uint8_t get_out_channel(int ch) {
      return outmap[ch].channel;
    }
    int8_t get_out_transpose(int ch) {
      return outmap[ch].transpose;
    }
    bool in_out_range(int ch, int note) {
      return (note >= outmap[ch].range_low && note <= outmap[ch].range_high);
    }

    void UpdateMidiChannelFilter() {
        uint16_t filter = 0;
        bool omni = false;
        for (auto &map : mapping) {
            if (map.function == HEM_MIDI_NOOP) continue;
            if (map.channel < 16) filter |= (1 << map.channel);
            else omni = true;
        }
        midi_channel_filter = filter;
        any_channel_omni = omni;
    }

    bool CheckMidiChannelFilter(const uint8_t m_ch) {
        return any_channel_omni || midi_channel_filter & (1 << m_ch);
    }

    void UpdateMaxPolyphony() { // find max voice number to determine how much to buffer
        int voice = 0;
        for (auto &map : mapping) {
            if (map.function == HEM_MIDI_NOOP) continue;
            if (map.dac_polyvoice > voice) voice = map.dac_polyvoice;
        }
        if (max_voice != voice+1) {
            ClearPolyBuffer();
            max_voice = voice+1;
        }
    }

    bool CheckPolyVoice(const uint8_t voice) {
        return (poly_buffer[voice].gate);
    }

    int FindNextAvailPolyVoice(const uint8_t note) {
        if (max_voice == 1) return 0;

        switch (poly_mode) {
            case POLY_RESET:
                for (int v = 0; v < max_voice; ++v)
                    if (!CheckPolyVoice(v)) return v;
                return max_voice - 1;
                break;
            case POLY_REUSE:
                for (int v = 0; v < max_voice; ++v)
                    if (poly_buffer[v].note == note) return v;
                // fallthrough
            case POLY_ROTATE:
                for (int v = 0; v < max_voice; ++v) {
                    if (++poly_rotate_index >= max_voice) poly_rotate_index = 0;
                    if (!CheckPolyVoice(poly_rotate_index)) return poly_rotate_index;
                }
                // if no voices empty
                if (++poly_rotate_index >= max_voice) poly_rotate_index = 0;
                return poly_rotate_index;
                break;
            default:
                return 0;
        }
    }

    int FindPolyNoteIndex(const uint8_t note) {
        for (int v = 0; v < max_voice; ++v)
            if (poly_buffer[v].note == note) return v;
        return -1;
    }

    void WritePolyNoteData(const uint8_t note, const uint8_t vel, const uint8_t voice) {
        poly_buffer[voice].note = note;
        poly_buffer[voice].vel = vel;
        poly_buffer[voice].gate = 1;
    }

    void ClearPolyVoice(const uint8_t voice) {
        poly_buffer[voice].vel = 0;
        poly_buffer[voice].gate = 0;
    }

    void PolyBufferPush(const uint8_t m_ch, const uint8_t note, const uint8_t vel) {
        if (CheckMidiChannelFilter(m_ch))
            WritePolyNoteData(note, vel, FindNextAvailPolyVoice(note));
    }

    void PolyBufferPop(const uint8_t m_ch, const uint8_t note) {
        if (CheckMidiChannelFilter(m_ch)) {
            for (uint8_t v = 0; v < max_voice; ++v) {
                if (poly_buffer[v].note == note) ClearPolyVoice(v);
            }
        }
    }

    void ClearPolyBuffer() {
        for (int ch = 0; ch < DAC_CHANNEL_COUNT; ++ch) {
            ClearPolyVoice(ch);
        }
    }

    // ADAPTATION: vendor used std::remove_if + buffer.erase; the inline
    // NoteBuffer exposes erase_note for the same effect.
    void RemoveNoteData(NoteBuffer &buffer, const uint8_t note) {
        buffer.erase_note(note);
    }

    void MonoBufferPush(const uint8_t m_ch, const uint8_t note, const uint8_t vel) {
        if (CheckMidiChannelFilter(m_ch)) {
            RemoveNoteData(note_buffer[m_ch], note); // if new note is already in buffer, promote to latest and update velocity
            note_buffer[m_ch].push_back({note, vel}); // else just append to the end
        }
    }

    void MonoBufferPop(const uint8_t m_ch, const uint8_t note) {
        if (CheckMidiChannelFilter(m_ch)) {
            RemoveNoteData(note_buffer[m_ch], note);
            if (note_buffer[m_ch].size() == 0) note_buffer[m_ch].shrink_to_fit(); // free up memory when MIDI is not used
        }
    }

    void ClearMonoBuffer(const int8_t m_ch = -1) {
        if (m_ch > 0) {
            note_buffer[m_ch].clear();
            note_buffer[m_ch].shrink_to_fit();
        } else { // clear on all channels if no args passed
            for (uint8_t c = 0; c < 16; ++c) {
                note_buffer[c].clear();
                note_buffer[c].shrink_to_fit();
            }
        }
    }

    int GetNoteFirst(NoteBuffer &buffer) {
        return buffer.front().note;
    }

    int GetNoteLast(NoteBuffer &buffer) {
        return buffer.back().note;
    }

    int GetNoteLastInv(NoteBuffer &buffer) {
        return 127 - buffer.back().note;
    }

    int GetNoteMin(NoteBuffer &buffer) {
        uint8_t m = 127;
        for (auto const &data : buffer) {
            if (data.note < m) m = data.note;
        }
        return m;
    }

    int GetNoteMax(NoteBuffer &buffer) {
        uint8_t m = 0;
        for (auto const &data : buffer) {
            if (data.note > m) m = data.note;
        }
        return m;
    }

    int GetVel(NoteBuffer &buffer, const int n) {
        return buffer[buffer.size()-n].vel;
    }

    void ClearSustainLatch(int8_t m_ch = -1) {
        if (m_ch > 0) sustain_latch &= ~(1 << m_ch);
        else { // clear on all channels if no args passed
            for (uint8_t c = 0; c < 16; ++c)
                sustain_latch &= ~(1 << c);
        }
    }

    bool CheckSustainLatch(const uint8_t m_ch) {
        return sustain_latch & (1 << m_ch);
    }

    // MIDI output stuff
    int outchan_last[DAC_CHANNEL_COUNT];
    uint8_t current_note[16]; // note number, per MIDI channel
    uint8_t current_ccval[DAC_CHANNEL_COUNT]; // level 0 - 127, per DAC channel
    int note_countdown[DAC_CHANNEL_COUNT];
    int last_cv[DAC_CHANNEL_COUNT];
    bool clocked[DAC_CHANNEL_COUNT];
    bool gate_high[DAC_CHANNEL_COUNT];
    bool changed_cv[DAC_CHANNEL_COUNT];

    // Logging
    MIDIMessage log[7];
    int log_index;

    void UpdateLog(const MIDIMessage msg) {
        log[log_index++] = msg;
        if (log_index == 7) {
            for (int i = 0; i < 6; i++) {
                memcpy(&log[i], &log[i+1], sizeof(log[i+1]));
            }
            log_index--;
        }
        last_msg_tick = OC::CORE::ticks;
    }
    void UpdateLog(uint8_t message, uint8_t data1, uint8_t data2) {
        UpdateLog({0, message, data1, data2});
    }

    // Bodies ported verbatim into shim/src/midi_frame.cpp.
    void ProcessMIDIMsg(const MIDIMessage msg);
    void Send(const SlewedValue *outvals);

    // ADAPTATION (Layer 0b): the Send* helpers route to NT_sendMidi* on ARM /
    // harness-recorded stubs on host instead of usbMIDI. Bodies in
    // shim/src/midi_frame.cpp.
    void SendAfterTouch(const uint8_t midi_ch, uint8_t val);
    void SendPitchBend(const uint8_t midi_ch, uint16_t bend);
    void SendCC(const uint8_t midi_ch, uint8_t ccnum, uint8_t val);
    void SendNoteOn(const uint8_t midi_ch, uint8_t note = 255, uint8_t vel = 100);
    void SendNoteOff(const uint8_t midi_ch, uint8_t note = 255, uint8_t vel = 0);
};

} // namespace HS
