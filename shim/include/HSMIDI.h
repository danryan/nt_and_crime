// HSMIDI.h shim stub for the disting NT host build.
//
// The vendor HSMIDI.h targets Teensyduino (usbMIDI, USBHost_t36, MIDI.h) which
// are unavailable on the arm-none-eabi and host toolchains used here. This stub
// satisfies the symbols that vendor HSClockManager.h needs to compile:
//
// 1. Task type - vendor OC_core.h defines it; our shim OC_core.h does not.
//    We define it here so it is visible before ClockManager's member
//    std::queue<Task> syncfn_queue is parsed.
// 2. usbMIDI stub - ClockManager::Start() and Stop() call
//    usbMIDI.sendRealTime(usbMIDI.Start/Stop). We provide a no-op stub so
//    the vendor ClockManager body compiles and links without Teensy headers.
// 3. HemMidiType and related HS namespace enums from vendor HSMIDI.h are
//    provided verbatim so any future applet that includes this header
//    gets the expected symbols.
//
// Guard matches vendor HSMIDI.h so double-inclusion is blocked.

#ifndef HSMIDI_H
#define HSMIDI_H

#include <functional>
#include <queue>
#include <cstdint>
#include "OC_DAC.h"

// Task: used by HSClockManager::BeatSync / syncfn_queue.
// Vendor OC_core.h defines this; our shim OC_core.h does not (it only
// provides OC::CORE::ticks). Define it here so the ClockManager class
// body compiles when our OC_core.h is substituted for vendor OC_core.h.
using Task = std::function<void()>;

// ---------------------------------------------------------------------------
// usbMIDI stub
// ---------------------------------------------------------------------------
// ClockManager::Start() and Stop() call usbMIDI.sendRealTime(usbMIDI.Start)
// and usbMIDI.sendRealTime(usbMIDI.Stop). On the NT host and arm-none-eabi
// build we have no USB MIDI; these are no-ops.

struct _UsbMidiStub {
    // MIDI real-time message constants (match Teensyduino usbMIDI values).
    static constexpr uint8_t NoteOff = 0x80;
    static constexpr uint8_t NoteOn = 0x90;
    static constexpr uint8_t AfterTouchPoly = 0xA0;
    static constexpr uint8_t ControlChange = 0xB0;
    static constexpr uint8_t AfterTouchChannel = 0xD0;
    static constexpr uint8_t PitchBend = 0xE0;
    static constexpr uint8_t SystemExclusive = 0xF0;
    static constexpr uint8_t Clock = 0xF8;
    static constexpr uint8_t Start = 0xFA;
    static constexpr uint8_t Stop = 0xFC;

    // No-op stubs for methods called by ClockManager.
    void sendRealTime(uint8_t) {}
    bool read() { return false; }
    uint8_t getType() { return 0; }
    uint8_t* getSysExArray() { return nullptr; }
    void send_now() {}
    void sendSysEx(uint32_t, const uint8_t*) {}
};

// Global singleton matching Teensyduino naming convention.
// static to avoid inline variable (C++17 feature); the stub is stateless so
// per-TU copies are harmless on the host build.
static _UsbMidiStub usbMIDI;

// ---------------------------------------------------------------------------
// HS namespace MIDI enums (vendored verbatim from HSMIDI.h for completeness)
// ---------------------------------------------------------------------------

namespace HS {

enum HemMidiType {
    HEM_MIDI_NOTE_ON = _UsbMidiStub::NoteOn,
    HEM_MIDI_NOTE_OFF = _UsbMidiStub::NoteOff,
    HEM_MIDI_CC = _UsbMidiStub::ControlChange,
    HEM_MIDI_AFTERTOUCH_CHANNEL = _UsbMidiStub::AfterTouchChannel,
    HEM_MIDI_AFTERTOUCH_POLY = _UsbMidiStub::AfterTouchPoly,
    HEM_MIDI_PITCHBEND = _UsbMidiStub::PitchBend,
    HEM_MIDI_SYSEX = _UsbMidiStub::SystemExclusive,
    HEM_MIDI_CLOCK = _UsbMidiStub::Clock,
    HEM_MIDI_START = _UsbMidiStub::Start,
    HEM_MIDI_STOP = _UsbMidiStub::Stop,
};

const char* const midi_note_numbers[128] = {
    "C-1","C#-1","D-1","D#-1","E-1","F-1","F#-1","G-1","G#-1","A-1","A#-1","B-1",
    "C0","C#0","D0","D#0","E0","F0","F#0","G0","G#0","A0","A#0","B0",
    "C1","C#1","D1","D#1","E1","F1","F#1","G1","G#1","A1","A#1","B1",
    "C2","C#2","D2","D#2","E2","F2","F#2","G2","G#2","A2","A#2","B2",
    "C3","C#3","D3","D#3","E3","F3","F#3","G3","G#3","A3","A#3","B3",
    "C4","C#4","D4","D#4","E4","F4","F#4","G4","G#4","A4","A#4","B4",
    "C5","C#5","D5","D#5","E5","F5","F#5","G5","G#5","A5","A#5","B5",
    "C6","C#6","D6","D#6","E6","F6","F#6","G6","G#6","A6","A#6","B6",
    "C7","C#7","D7","D#7","E7","F7","F#7","G7","G#7","A7","A#7","B7",
    "C8","C#8","D8","D#8","E8","F8","F#8","G8","G#8","A8","A#8","B8",
    "C9","C#9","D9","D#9","E9","F9","F#9","G9"
};

const char* const midi_channels[17] = {
    " 1", " 2", " 3", " 4", " 5", " 6", " 7", " 8",
    " 9", "10", "11", "12", "13", "14", "15", "16", "Om"
};

enum MIDIFunctions : uint8_t {
    HEM_MIDI_NOOP = 0,
    HEM_MIDI_NOTE_OUT,
    HEM_MIDI_NOTE_POLY_OUT,
    HEM_MIDI_NOTE_MIN_OUT,
    HEM_MIDI_NOTE_MAX_OUT,
    HEM_MIDI_NOTE_PEDAL_OUT,
    HEM_MIDI_NOTE_INV_OUT,
    HEM_MIDI_TRIG_OUT,
    HEM_MIDI_TRIG_1ST_OUT,
    HEM_MIDI_TRIG_ALWAYS_OUT,
    HEM_MIDI_GATE_OUT,
    HEM_MIDI_GATE_POLY_OUT,
    HEM_MIDI_GATE_INV_OUT,
    HEM_MIDI_VEL_OUT,
    HEM_MIDI_VEL_POLY_OUT,
    HEM_MIDI_CC_OUT,
    HEM_MIDI_AT_CHAN_OUT,
    HEM_MIDI_AT_KEY_POLY_OUT,
    HEM_MIDI_PB_OUT,
    HEM_MIDI_RUN_OUT,
    HEM_MIDI_START_OUT,
    HEM_MIDI_CLOCK_OUT,
    HEM_MIDI_CLOCK_8_OUT,
    HEM_MIDI_CLOCK_16_OUT,
    HEM_MIDI_CLOCK_24_OUT,
    HEM_MIDI_LEARN,
    HEM_MIDI_FN_COUNT,
    HEM_MIDI_MAX_FUNCTION = HEM_MIDI_CLOCK_24_OUT,
};

const char* const midi_fn_name[HEM_MIDI_FN_COUNT] = {
    "None",
    "Note", "PolyN", "LoNote", "HiNote", "PdlNote", "InvNote",
    "Trig", "Trig1st", "TrgAlws",
    "Gate", "PolyG", "GateInv",
    "Veloc", "PolyV",
    "CC#",
    "ChnAft", "KeyAft",
    "Bend",
    "Run", "Start",
    "Clk-2", "Clk-4", "Clk-8", "Clk24",
    "(learn)",
};

enum MIDIPolyMode : uint8_t {
    POLY_RESET = 0,
    POLY_ROTATE,
    POLY_REUSE,
    POLY_LAST = POLY_REUSE
};

const char* const midi_poly_mode_name[POLY_LAST + 1] = {
    "Reset", "Rotate", "Reuse"
};

} // namespace HS

// ---------------------------------------------------------------------------
// SysEx data structures (vendored verbatim from HSMIDI.h)
// ---------------------------------------------------------------------------

#define SYSEX_DATA_MAX_SIZE 60

struct _SysExData {
    int size;
    uint8_t data[SYSEX_DATA_MAX_SIZE];

    void set_data(int size_, uint8_t data_[])
    {
        size = size_;
        for (int i = 0; i < size; i++) data[i] = data_[i];
    }

    _SysExData unpack()
    {
        uint8_t udata[SYSEX_DATA_MAX_SIZE];
        uint8_t packbyte = 0;
        uint8_t pos = 0;
        uint8_t usize = 0;
        uint8_t c;
        for (int ixp = 0; ixp < size; ixp++)
        {
            c = data[ixp];
            if (pos == 0) {
                packbyte = c;
            } else {
                if (packbyte & (1 << (pos - 1))) {c |= 0x80;}
                udata[usize++] = c;
            }
            pos++;
            pos &= 0x07;
            if (usize > SYSEX_DATA_MAX_SIZE) break;
        }
        _SysExData unpacked;
        unpacked.set_data(usize, udata);
        return unpacked;
    }

    _SysExData pack()
    {
        uint8_t pdata[SYSEX_DATA_MAX_SIZE];
        uint8_t packbyte = 0;
        uint8_t pos = 0;
        uint8_t psize = 0;
        uint8_t packet[7];
        uint8_t c;
        for (int ixu = 0; ixu < size; ixu++)
        {
            c = data[ixu];
            if (pos == 7) {
                pdata[psize++] = packbyte;
                for (int i = 0; i < pos; i++) pdata[psize++] = packet[i];
                packbyte = 0;
                pos = 0;
            }
            if (c & 0x80) {
                packbyte += (1 << pos);
                c &= 0x7f;
            }
            packet[pos] = c;
            pos++;
            if ((psize + 8) > SYSEX_DATA_MAX_SIZE) break;
        }
        pdata[psize++] = packbyte;
        for (int i = 0; i < pos; i++) pdata[psize++] = packet[i];
        _SysExData packed;
        packed.set_data(psize, pdata);
        return packed;
    }
};
using SysExData = _SysExData;
using UnpackedData = _SysExData;
using PackedData = _SysExData;

// MIDIQuantizer canonical definition lives in quant/MIDIQuantizer.h
// (dep-quant owns it). Layer 2 integration deduplicates by including
// the canonical definition here so HSClockManager.h continues to compile
// against the single class without ODR conflict.
#include "quant/MIDIQuantizer.h"

#endif /* HSMIDI_H */
