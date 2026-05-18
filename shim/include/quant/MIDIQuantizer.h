#pragma once
#include "OC_DAC.h"

/*
 * MIDI Quantizer: Converts pitch CV values to MIDI note numbers,
 * and vice versa. CV values are (12 << 7) steps per volt, or
 * 128 steps per semitone.
 */
class MIDIQuantizer {
public:
    /* Given a pitch CV value, return the MIDI note number */
    static uint8_t NoteNumber(int cv, int transpose = 0, uint8_t bias = OC::DAC::kOctaveZero) {
        // CV controllers might be right on the border between voltages, so provide 1/4 tone offset
        if (cv > 0) cv += 32;
        if (cv < 0) cv -= 32;
        int octave = cv / (12 << 7);
        int semitone = (cv % (12 << 7)) / 128;
        int midi_note_number = (octave * 12) + semitone + transpose + (12 * bias);
        if (midi_note_number > 127) midi_note_number = 127;
        if (midi_note_number < 0) midi_note_number = 0;
        return static_cast<uint8_t>(midi_note_number);
    }

    /* Given a MIDI note number, return the pitch CV value */
    static int CV(uint8_t midi_note_number, int transpose = 0, uint8_t bias = OC::DAC::kOctaveZero) {
        int octave = midi_note_number / 12;
        int semitone = midi_note_number % 12;
        int cv = (octave * (12 << 7))
          + (semitone * 128)
          + (transpose * 128)
          - (bias * (12 << 7)); // floor depends on output bias
        return cv;
    }
};
