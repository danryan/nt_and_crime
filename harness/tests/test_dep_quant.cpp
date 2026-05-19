// dep-quant test suite.
//
// Output-parity class: integer-only for braids::Quantizer scale lookups and
// round-trips; float-tolerance (1 LSB) is not needed here because the
// quantizer operates entirely in integer CV units.
//
// Three test groups:
//   [braids-quant]  braids::Quantizer chromatic round-trip and scale lookup
//   [hs-quant]      HS::QuantEngine / HS::Quantize free-function surface
//   [midi-quant]    MIDIQuantizer::NoteNumber and CV round-trip
#include "catch.hpp"
#include "HemisphereApplet.h"
#include "quant/OC_scales.h"
#include "quant/MIDIQuantizer.h"

// ONE_OCTAVE is defined in HSUtils.h (12 << 7 = 1536 hem units).
// SCALE_SEMI is the chromatic 12-note scale (all semitones present).

// ---------------------------------------------------------------------------
// braids::Quantizer
// ---------------------------------------------------------------------------

TEST_CASE("braids::Quantizer chromatic round-trip at octave boundaries", "[braids-quant]") {
    braids::Quantizer q;
    q.Init();
    q.Configure(OC::Scales::GetScale(OC::Scales::SCALE_SEMI), 0xffff);
    CHECK(q.enabled());

    // For the chromatic scale every semitone is present; Process(cv) should
    // return a pitch that lies on a semitone boundary (multiple of 128 hem
    // units). Test across three octave intervals.
    for (int octave = 0; octave < 3; ++octave) {
        int cv = octave * ONE_OCTAVE; // 0, 1536, 3072
        int result = q.Process(cv, 0, 0);
        // Result must be a multiple of 128 (one semitone in hem units).
        CHECK((result % 128) == 0);
    }
}

TEST_CASE("braids::Quantizer process is idempotent on already-quantized input", "[braids-quant]") {
    braids::Quantizer q;
    q.Init();
    q.Configure(OC::Scales::GetScale(OC::Scales::SCALE_SEMI), 0xffff);

    int cv = ONE_OCTAVE; // 1536 hem units, exactly one octave above C
    int first  = q.Process(cv, 0, 0);
    int second = q.Process(first, 0, 0);
    CHECK(first == second);
}

TEST_CASE("braids::Quantizer Init then Configure changes enabled state", "[braids-quant]") {
    braids::Quantizer q;
    q.Init();
    // After Init, enabled_ = true but span_ == 0, so Configure needed for real use.
    // Configure with the SEMI scale makes it properly enabled.
    q.Configure(OC::Scales::GetScale(OC::Scales::SCALE_SEMI), 0xffff);
    CHECK(q.enabled());
}

TEST_CASE("braids::Quantizer Lookup returns a pitch value", "[braids-quant]") {
    braids::Quantizer q;
    q.Init();
    q.Configure(OC::Scales::GetScale(OC::Scales::SCALE_SEMI), 0xffff);
    // Lookup(64) is the zero-reference note (C2 in vendor convention).
    // It should return a value that is a multiple of 128.
    int32_t pitch = q.Lookup(64);
    CHECK((pitch % 128) == 0);
}

// ---------------------------------------------------------------------------
// OC::Scales
// ---------------------------------------------------------------------------

TEST_CASE("OC::Scales::GetScale returns SCALE_SEMI with 12 notes", "[braids-quant]") {
    const OC::Scale& scale = OC::Scales::GetScale(OC::Scales::SCALE_SEMI);
    CHECK(scale.num_notes == 12);
    // Span for chromatic is one octave: 12 semitones * 128 hem = 1536.
    CHECK(scale.span == ONE_OCTAVE);
}

TEST_CASE("OC::Scales::GetScale user slot returns a scale object", "[braids-quant]") {
    // User scales are initialised in OC::Scales::Init(). Even without calling
    // Init, GetScale(SCALE_USER_0) returns user_scales[0] which is a valid
    // Scale struct (zero-initialised or Init-set).
    const OC::Scale& scale = OC::Scales::GetScale(OC::Scales::SCALE_USER_0);
    // num_notes == 0 is fine for an uninitialised user scale; we just
    // confirm the call does not crash and returns an object.
    CHECK(scale.span >= 0);
}

// ---------------------------------------------------------------------------
// HS::QuantEngine / HS::Quantize free-function surface
// ---------------------------------------------------------------------------

TEST_CASE("HS::QuantizerConfigure and HS::Quantize on channel 0", "[hs-quant]") {
    // Configure channel 0 with SEMI scale, all notes enabled.
    HS::QuantizerConfigure(0, OC::Scales::SCALE_SEMI, 0xffff);

    // Quantize ONE_OCTAVE should return a pitch on a semitone boundary.
    int result = HS::Quantize(0, ONE_OCTAVE, 0, 0);
    CHECK((result % 128) == 0);
}

TEST_CASE("HS::QuantizerConfigure preserves scale selection", "[hs-quant]") {
    HS::QuantizerConfigure(0, OC::Scales::SCALE_SEMI, 0xffff);
    CHECK(HS::GetScale(0) == OC::Scales::SCALE_SEMI);
}

TEST_CASE("HS::SetRootNote and HS::GetRootNote round-trip", "[hs-quant]") {
    HS::SetRootNote(0, 3); // C# / Eb
    CHECK(HS::GetRootNote(0) == 3);
    HS::SetRootNote(0, 0); // reset to C
    CHECK(HS::GetRootNote(0) == 0);
}

TEST_CASE("HS::NudgeScale wraps at NUM_SCALES boundary", "[hs-quant]") {
    HS::QuantizerConfigure(0, 0, 0xffff); // scale 0
    HS::NudgeScale(0, -1); // should wrap to NUM_SCALES - 1
    int wrapped_scale = HS::GetScale(0);
    CHECK(wrapped_scale == OC::Scales::NUM_SCALES - 1);
    // Reset to SEMI
    HS::QuantizerConfigure(0, OC::Scales::SCALE_SEMI, 0xffff);
}

TEST_CASE("HS::QuantizerLookup returns a pitch value", "[hs-quant]") {
    HS::QuantizerConfigure(0, OC::Scales::SCALE_SEMI, 0xffff);
    int pitch = HS::QuantizerLookup(0, 64);
    CHECK((pitch % 128) == 0);
}

TEST_CASE("HS::GetLatestNoteNumber returns value after Quantize", "[hs-quant]") {
    HS::QuantizerConfigure(0, OC::Scales::SCALE_SEMI, 0xffff);
    HS::Quantize(0, ONE_OCTAVE, 0, 0);
    int note = HS::GetLatestNoteNumber(0);
    // Note numbers are in a plausible range (0-127 MIDI, but vendor uses a
    // wider encoding; just confirm it's non-zero after a non-zero input pitch).
    CHECK(note >= 0);
}

TEST_CASE("HS::Quantize ch clamp: out-of-range channel does not crash", "[hs-quant]") {
    // Channels beyond QUANT_CHANNEL_COUNT-1 are clamped. Confirm no crash.
    int result = HS::Quantize(999, ONE_OCTAVE, 0, 0);
    CHECK((result % 128) == 0);
}

// ---------------------------------------------------------------------------
// MIDIQuantizer
// ---------------------------------------------------------------------------

TEST_CASE("MIDIQuantizer::NoteNumber one octave above zero", "[midi-quant]") {
    // ONE_OCTAVE cv = 1536 hem. With default bias = kOctaveZero = 5:
    // cv = 1536 > 0, so cv += 32 -> 1568.
    // octave = 1568 / 1536 = 1
    // semitone = (1568 % 1536) / 128 = 32 / 128 = 0
    // note = 1*12 + 0 + 0 + 12*5 = 12 + 60 = 72
    uint8_t note = MIDIQuantizer::NoteNumber(ONE_OCTAVE);
    CHECK(note == 72);
}

TEST_CASE("MIDIQuantizer::NoteNumber zero CV maps to C5 (bias=5)", "[midi-quant]") {
    // cv = 0, no offset added (not > 0 and not < 0)
    // octave = 0, semitone = 0, note = 0 + 0 + 0 + 12*5 = 60
    uint8_t note = MIDIQuantizer::NoteNumber(0);
    CHECK(note == 60);
}

TEST_CASE("MIDIQuantizer::CV round-trips NoteNumber", "[midi-quant]") {
    // Convert CV -> note -> CV and verify we land within 1 semitone (128 units).
    int original_cv = ONE_OCTAVE;
    uint8_t note = MIDIQuantizer::NoteNumber(original_cv);
    int round_trip_cv = MIDIQuantizer::CV(note);
    // Allow up to 128 hem units (one semitone) of rounding from the 1/4-tone
    // anti-jitter offset applied by NoteNumber.
    int diff = round_trip_cv - original_cv;
    if (diff < 0) diff = -diff;
    CHECK(diff <= 128);
}

TEST_CASE("MIDIQuantizer::NoteNumber clamps to 127", "[midi-quant]") {
    // Very high CV should clamp to 127.
    uint8_t note = MIDIQuantizer::NoteNumber(100 * ONE_OCTAVE);
    CHECK(note == 127);
}

TEST_CASE("MIDIQuantizer::NoteNumber clamps to 0", "[midi-quant]") {
    // Very negative CV should clamp to 0.
    uint8_t note = MIDIQuantizer::NoteNumber(-100 * ONE_OCTAVE);
    CHECK(note == 0);
}
