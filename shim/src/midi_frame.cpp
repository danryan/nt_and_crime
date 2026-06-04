// midi_frame.cpp -- bodies for the shim-owned HS::MIDIFrame port.
//
// ProcessMIDIMsg and Send are ported verbatim from the vendor
// vendor/O_C-Phazerville/software/src/HSIOFrame.cpp, with two adaptations:
//   - usbMIDI.* status constants become HS::HemMidiType (raw status nibbles).
//   - Send* routes to the NT MIDI API (ARM) or harness-recorded stubs (host),
//     never usbMIDI.
//
// Send-side MIDI destination: kNT_destinationBreakout | kNT_destinationUSB.
// These are the two physical/standard MIDI outputs a hardware NT exposes
// (the TRS breakout jack and the USB device port). SelectBus and Internal are
// routing-graph targets, not "send it out a wire" destinations, so the
// faithful equivalent of the vendor "send to the world" behaviour is the
// breakout + USB pair.

#include "../include/HSMIDIFrame.h"

#if defined(__arm__)
#include <distingnt/api.h>
#endif

namespace {
// Free ProportionCV mirroring the HemisphereApplet member / vendor HSUtils
// free function. MIDIFrame is not a HemisphereApplet, so it needs a non-member
// here. Kept file-local to avoid any ODR clash with the member.
int midi_proportion_cv(int cv_value, int max_pixels, int max_cv = HEMISPHERE_MAX_CV) {
    long prop = (long)cv_value * max_pixels / (max_cv > 0 ? max_cv : 1);
    int v = (int)prop;
    if (v < 0) v = 0;
    if (v > max_pixels) v = max_pixels;
    return v;
}
} // namespace

// ---------------------------------------------------------------------------
// Layer 0b: send side -> NT MIDI.
//
// On ARM the firmware resolves NT_sendMidi2ByteMessage / 3ByteMessage. On the
// host build these symbols are provided by the harness (harness/src/nt_runtime.cpp)
// and record into a capture buffer that nt::midi_sent() exposes.
// ---------------------------------------------------------------------------
#if !defined(__arm__)
extern "C" void NT_sendMidi2ByteMessage(uint32_t destination, uint8_t b0, uint8_t b1);
extern "C" void NT_sendMidi3ByteMessage(uint32_t destination, uint8_t b0, uint8_t b1, uint8_t b2);
#endif

namespace {
constexpr uint32_t kMidiDest = kNT_destinationBreakout | kNT_destinationUSB;
}

void HS::MIDIFrame::SendAfterTouch(const uint8_t midi_ch, uint8_t val) {
    NT_sendMidi2ByteMessage(kMidiDest, (uint8_t)(0xD0 | (midi_ch & 0x0F)), val);
}

void HS::MIDIFrame::SendPitchBend(const uint8_t midi_ch, uint16_t bend) {
    NT_sendMidi3ByteMessage(kMidiDest, (uint8_t)(0xE0 | (midi_ch & 0x0F)),
                            (uint8_t)(bend & 0x7F), (uint8_t)((bend >> 7) & 0x7F));
}

void HS::MIDIFrame::SendCC(const uint8_t midi_ch, uint8_t ccnum, uint8_t val) {
    NT_sendMidi3ByteMessage(kMidiDest, (uint8_t)(0xB0 | (midi_ch & 0x0F)), ccnum, val);
}

void HS::MIDIFrame::SendNoteOn(const uint8_t midi_ch, uint8_t note, uint8_t vel) {
    if (note > 127) note = current_note[midi_ch];
    else current_note[midi_ch] = note;
    NT_sendMidi3ByteMessage(kMidiDest, (uint8_t)(0x90 | (midi_ch & 0x0F)), note, vel);
}

void HS::MIDIFrame::SendNoteOff(const uint8_t midi_ch, uint8_t note, uint8_t vel) {
    if (note > 127) note = current_note[midi_ch];
    NT_sendMidi3ByteMessage(kMidiDest, (uint8_t)(0x80 | (midi_ch & 0x0F)), note, vel);
}

// ---------------------------------------------------------------------------
// Layer 0a: ProcessMIDIMsg -- verbatim port of HSIOFrame.cpp:7 with usbMIDI.*
// status constants swapped for HS::HemMidiType / raw nibbles.
//
// Realtime status to HemMidiType mapping:
//   usbMIDI.Clock        = 0xF8 = HEM_MIDI_CLOCK
//   usbMIDI.Start        = 0xFA = HEM_MIDI_START
//   usbMIDI.Continue     = 0xFB  (not a HemMidiType; raw 0xFB)
//   usbMIDI.Stop         = 0xFC = HEM_MIDI_STOP
//   usbMIDI.SystemReset  = 0xFF  (raw)
// Channel-voice status (high nibble) to HemMidiType:
//   NoteOn 0x90, NoteOff 0x80, ControlChange 0xB0, AfterTouchPoly 0xA0,
//   AfterTouchChannel 0xD0, PitchBend 0xE0.
// The firmware's midiMessage callback delivers the status byte already masked
// to its high nibble (b0 & 0xF0), so the switch matches the HemMidiType values.
// ---------------------------------------------------------------------------

// Raw realtime status bytes not represented by a HemMidiType enumerator.
#define HEM_MIDI_CONTINUE 0xFB
#define HEM_MIDI_SYSTEM_RESET 0xFF

void HS::MIDIFrame::ProcessMIDIMsg(const MIDIMessage msg) {
    const uint8_t m_ch = msg.channel - 1;

    switch (msg.message) { // System Real Time messages
        case HEM_MIDI_CLOCK:
            clock_q = (clock_count % (24/MIDI_CLOCK_PPQN) == 0); // for internal sync @ 2ppqn
            ++clock_count;
            for(int ch = 0; ch < MIDIMAP_MAX; ++ch) {
                mapping[ch].ProcessClock(clock_count);
            }
            if (clock_count == 24) clock_count = 0;
            return;
            break;

        case HEM_MIDI_START:
        case HEM_MIDI_CONTINUE: // treat Continue like Start
            start_q = 1;
            clock_count = 0;
            clock_run = true;

            for(int ch = 0; ch < MIDIMAP_MAX; ++ch) {
                if (mapping[ch].function == HEM_MIDI_START_OUT) {
                    mapping[ch].ClockOut();
                }
            }

            // UpdateLog(message, data1, data2);
            return;
            break;

        case HEM_MIDI_STOP:
        case HEM_MIDI_SYSTEM_RESET:
            stop_q = 1;
            clock_run = false;
            // a way to reset stuck notes
            ClearMonoBuffer();
            ClearSustainLatch();
            ClearPolyBuffer();
            for (int ch = 0; ch < MIDIMAP_MAX; ++ch) {
                mapping[ch].output = 0;
                mapping[ch].trigout_countdown = 0;
            }
            return;
            break;
    }

    if (!CheckMidiChannelFilter(m_ch)) return;

    switch (msg.message) { // Channel Voice messages
        case HEM_MIDI_NOTE_ON:
            MonoBufferPush(m_ch, msg.data1, msg.data2);
            PolyBufferPush(m_ch, msg.data1, msg.data2);
            break;

        case HEM_MIDI_NOTE_OFF:
            MonoBufferPop(m_ch, msg.data1);
            PolyBufferPop(m_ch, msg.data1);
            break;
    }

    bool log_skip = false;
    uint8_t m_ch_prev = 255; // initialize to invalid channel

    for(int ch = 0; ch < MIDIMAP_MAX; ++ch) {
        MIDIMapping &map = mapping[ch];
        if (map.function == HEM_MIDI_NOOP) continue;

        // skip unwanted MIDI Channels
        if (map.channel != m_ch && map.channel != 16) continue;

        last_midi_channel = m_ch;

        // prevent duplicate log entries
        if (m_ch == m_ch_prev) log_skip = true;
        else log_skip = false;
        m_ch_prev = m_ch;

        bool log_this = false;

        switch (msg.message) {
            case HEM_MIDI_NOTE_ON: {
                if (map.function == HEM_MIDI_LEARN) {
                  //TODO: set range based on polyphony, or alternate learn modes
                  if (map.function_cc < 0) {
                    map.range_low = min(map.range_low, msg.data1);
                    map.range_high = max(map.range_high, msg.data1);
                  }
                  //map.function = HEM_MIDI_NOTE_OUT;
                  //map.function_cc = 0;
                  map.channel = msg.channel - 1;
                }
                if (!map.InRange(msg.data1)) break;
                map.semitone_mask = map.semitone_mask | (1u << (msg.data1 % 12));

                // Should this message go out on this channel?
                switch (map.function) { // note # output functions
                    case HEM_MIDI_NOTE_OUT:
                        map.output = MIDIQuantizer::CV(GetNoteLast(note_buffer[m_ch]));
                        break;

                    case HEM_MIDI_NOTE_POLY_OUT:
                        if (CheckPolyVoice(map.dac_polyvoice)) map.output = MIDIQuantizer::CV(poly_buffer[map.dac_polyvoice].note);
                        break;

                    case HEM_MIDI_NOTE_MIN_OUT:
                        map.output = MIDIQuantizer::CV(GetNoteMin(note_buffer[m_ch]));
                        break;

                    case HEM_MIDI_NOTE_MAX_OUT:
                        map.output = MIDIQuantizer::CV(GetNoteMax(note_buffer[m_ch]));
                        break;

                    case HEM_MIDI_NOTE_PEDAL_OUT:
                        map.output = MIDIQuantizer::CV(GetNoteFirst(note_buffer[m_ch]));
                        break;

                    case HEM_MIDI_NOTE_INV_OUT:
                        map.output = MIDIQuantizer::CV(GetNoteLastInv(note_buffer[m_ch]));
                        break;

                    case HEM_MIDI_TRIG_1ST_OUT:
                        if (note_buffer[m_ch].size() != 1) break;
                    case HEM_MIDI_TRIG_OUT:
                    case HEM_MIDI_TRIG_ALWAYS_OUT:
                        map.ClockOut();
                        break;

                    case HEM_MIDI_GATE_OUT:
                        map.output = PULSE_VOLTAGE * (12 << 7);
                        break;
                    case HEM_MIDI_GATE_INV_OUT:
                        map.output = 0;
                        break;
                    case HEM_MIDI_GATE_POLY_OUT:
                        if (CheckPolyVoice(map.dac_polyvoice)) map.output = PULSE_VOLTAGE * (12 << 7);
                        break;

                    case HEM_MIDI_VEL_OUT:
                        map.output = (note_buffer[m_ch].size() > 0) ? Proportion(GetVel(note_buffer[m_ch], 1), 127, HEMISPHERE_MAX_CV) : 0;
                        break;
                    case HEM_MIDI_VEL_POLY_OUT:
                        map.output = (CheckPolyVoice(map.dac_polyvoice)) ? Proportion(poly_buffer[map.dac_polyvoice].vel, 127, HEMISPHERE_MAX_CV) : 0;
                        break;
                }


                if (!log_skip) log_this = 1; // Log all MIDI notes. Other stuff is conditional.
                break;
            }
            case HEM_MIDI_NOTE_OFF: {
                if (!map.InRange(msg.data1)) break;
                map.semitone_mask = map.semitone_mask & ~(1u << (msg.data1 % 12));
                if (map.function == HEM_MIDI_LEARN && map.semitone_mask == 0) {
                  map.function = HEM_MIDI_NOTE_OUT;
                  map.function_cc = 0;
                }

                // don't update output when last note is released
                // or if sustain is engaged
                if (note_buffer[m_ch].size() > 0 && !CheckSustainLatch(m_ch)) {
                    switch(map.function) { // note # output functions
                        case HEM_MIDI_NOTE_OUT:
                            map.output = MIDIQuantizer::CV(GetNoteLast(note_buffer[m_ch]));
                            break;

                        case HEM_MIDI_NOTE_POLY_OUT:
                            if (CheckPolyVoice(map.dac_polyvoice)) map.output = MIDIQuantizer::CV(poly_buffer[map.dac_polyvoice].note);
                            break;

                        case HEM_MIDI_NOTE_MIN_OUT:
                            map.output = MIDIQuantizer::CV(GetNoteMin(note_buffer[m_ch]));
                            break;

                        case HEM_MIDI_NOTE_MAX_OUT:
                            map.output = MIDIQuantizer::CV(GetNoteMax(note_buffer[m_ch]));
                            break;

                        case HEM_MIDI_NOTE_PEDAL_OUT:
                            map.output = MIDIQuantizer::CV(GetNoteFirst(note_buffer[m_ch]));
                            break;

                        case HEM_MIDI_NOTE_INV_OUT:
                            map.output = MIDIQuantizer::CV(GetNoteLastInv(note_buffer[m_ch]));
                            break;
                    }
                }

                if (map.function == HEM_MIDI_TRIG_ALWAYS_OUT) map.ClockOut();

                if (!CheckSustainLatch(m_ch)) {
                    if (!(note_buffer[m_ch].size() > 0)) { // turn mono gate off, only when all notes are off
                        if (map.function == HEM_MIDI_GATE_OUT) map.output = 0;
                        if (map.function == HEM_MIDI_GATE_INV_OUT) map.output = PULSE_VOLTAGE * (12 << 7);
                    }
                    if (map.function == HEM_MIDI_GATE_POLY_OUT) {
                        if (!CheckPolyVoice(map.dac_polyvoice)) map.output = 0;
                    }
                }

                if (map.function == HEM_MIDI_VEL_OUT)
                    map.output = (note_buffer[m_ch].size() > 0) ? Proportion(GetVel(note_buffer[m_ch], 1), 127, HEMISPHERE_MAX_CV) : 0;
                if (map.function == HEM_MIDI_VEL_POLY_OUT)
                    map.output = (CheckPolyVoice(map.dac_polyvoice)) ? Proportion(poly_buffer[map.dac_polyvoice].vel, 127, HEMISPHERE_MAX_CV) : 0;

                if (!log_skip) log_this = 1;
                break;
            }
            case HEM_MIDI_CC: { // Modulation wheel or other CC
                // handle sustain pedal
                if (msg.data1 == 64) {
                    if (msg.data2 > 63) {
                        if (!CheckSustainLatch(m_ch)) sustain_latch |= (1 << m_ch);
                    } else {
                        ClearSustainLatch(m_ch);
                        if (!(note_buffer[m_ch].size() > 0)) {
                            switch (map.function) {
                                case HEM_MIDI_GATE_OUT:
                                case HEM_MIDI_GATE_POLY_OUT:
                                    map.output = 0;
                                    break;
                                case HEM_MIDI_GATE_INV_OUT:
                                    map.output = PULSE_VOLTAGE * (12 << 7);
                                    break;
                            }
                        }
                    }
                }

                if (map.function == HEM_MIDI_LEARN) {
                  map.function = HEM_MIDI_CC_OUT;
                  map.function_cc = msg.data1;
                  map.channel = msg.channel - 1;
                }

                if (map.function == HEM_MIDI_CC_OUT) {
                    if (map.function_cc < 0) { // auto-learn CC#
                      map.function_cc = msg.data1;
                    }
                    if (map.function_cc == msg.data1) {
                        map.output = Proportion(msg.data2, 127, HEMISPHERE_MAX_CV);
                        if (!log_skip) log_this = 1;
                    }
                }
                break;
            }
            case HEM_MIDI_AFTERTOUCH_POLY: {
                if (map.function == HEM_MIDI_AT_KEY_POLY_OUT) {
                    if (FindPolyNoteIndex(msg.data1) == map.dac_polyvoice)
                        map.output = Proportion(msg.data2, 127, HEMISPHERE_MAX_CV);
                    if (!log_skip) log_this = 1;
                }
                break;
            }
            case HEM_MIDI_AFTERTOUCH_CHANNEL: {
                if (map.function == HEM_MIDI_AT_CHAN_OUT) {
                    map.output = Proportion(msg.data1, 127, HEMISPHERE_MAX_CV);
                    if (!log_skip) log_this = 1;
                }
                break;
            }
            case HEM_MIDI_PITCHBEND: {
                if (map.function == HEM_MIDI_LEARN) {
                  map.function = HEM_MIDI_PB_OUT;
                  map.channel = msg.channel - 1;
                }
                if (map.function == HEM_MIDI_PB_OUT) {
                    int data = (msg.data2 << 7) + msg.data1 - 8192;
                    map.output = Proportion(data, 8192, HEMISPHERE_3V_CV);
                    if (!log_skip) log_this = 1;
                }
                break;
            }
        }
        if (log_this) UpdateLog(msg);
    }
}

void HS::MIDIFrame::Send(const SlewedValue *outvals) {
    // first pass - calculate things and turn off notes
    for (int i = 0; i < DAC_CHANNEL_COUNT; ++i) {
        const uint8_t midi_ch = outmap[i].channel;

        int input = outvals[i].get();
        gate_high[i] = input > (12 << 7);
        clocked[i] = (gate_high[i] && last_cv[i] < (12 << 7));
        if (abs(input - last_cv[i]) > HEMISPHERE_CHANGE_THRESHOLD) {
            changed_cv[i] = 1;
            last_cv[i] = input;
        } else changed_cv[i] = 0;

        switch (outmap[i].function) {
            case HEM_MIDI_NOTE_OUT:
                if (changed_cv[i]) {
                    // a note has changed, turn the last one off first
                    SendNoteOff(outchan_last[i]);
                    current_note[midi_ch] = MIDIQuantizer::NoteNumber( input );
                }
                break;

            case HEM_MIDI_GATE_OUT:
                if (!gate_high[i] && changed_cv[i])
                    SendNoteOff(midi_ch);
                break;

            case HEM_MIDI_CC_OUT:
            {
                const uint8_t newccval = midi_proportion_cv(abs(input), 127);
                if (newccval != current_ccval[i]) {
                  SendCC(midi_ch, outmap[i].function_cc, newccval);
                  current_ccval[i] = newccval;
                }
                break;
            }
        }

        // Handle clock pulse timing
        if (note_countdown[i] > 0) {
            if (--note_countdown[i] == 0) SendNoteOff(outchan_last[i]);
        }
    }

    // 2nd pass - send eligible notes
    for (int i = 0; i < 2; ++i) {
        const int chA = i*2;
        const int chB = chA + 1;

        if (outmap[chB].function == HEM_MIDI_GATE_OUT) {
            if (clocked[chB]) {
                SendNoteOn(outmap[chB].channel);
                // no countdown
                outchan_last[chB] = outmap[chB].channel;
            }
        } else if (outmap[chA].function == HEM_MIDI_NOTE_OUT) {
            if (changed_cv[chA]) {
                SendNoteOn(outmap[chA].channel);
                note_countdown[chA] = HEMISPHERE_CLOCK_TICKS * trig_length;
                outchan_last[chA] = outmap[chA].channel;
            }
        }
    }
}
