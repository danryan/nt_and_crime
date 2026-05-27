// HS:: quantizer engine: global pool definition and free-function
// implementations. These mirror vendor HSUtils.h:200-235 and HSUtils.cpp.
#include "HSUtils.h"
#include "OC_scales.h"

namespace HS {

QuantEngine q_engine[QUANT_CHANNEL_COUNT];

QuantEngine& GetQuantEngine(int ch) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    return q_engine[ch];
}

int GetLatestNoteNumber(int ch) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    return q_engine[ch].quantizer.GetLatestNoteNumber();
}

int Quantize(int ch, int cv, int root, int transpose) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    return q_engine[ch].Process(cv, root, transpose);
}

int QuantizerLookup(int ch, int note) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    return q_engine[ch].Lookup(note);
}

void QuantizerConfigure(int ch, int scale, uint16_t mask) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    q_engine[ch].Configure(scale, mask);
}

int GetScale(int ch) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    return q_engine[ch].scale;
}

int GetRootNote(int ch) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    return q_engine[ch].root_note;
}

int SetRootNote(int ch, int root) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    CONSTRAIN(root, 0, 11);
    q_engine[ch].root_note = root;
    return root;
}

void NudgeRootNote(int ch, int dir) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    int root = q_engine[ch].root_note + dir;
    if (root > 11) root = 0;
    if (root < 0) root = 11;
    q_engine[ch].root_note = root;
}

void NudgeOctave(int ch, int dir) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    q_engine[ch].octave += dir;
    CONSTRAIN(q_engine[ch].octave, -4, 4);
}

void NudgeScale(int ch, int dir) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    q_engine[ch].NudgeScale(dir);
}

void QuantizerEdit(int ch) {
    // Stub: no display subsystem in shim. Vendor body opens a popup UI.
    (void)ch;
}

void SetScale(int ch, int scale) {
    CONSTRAIN(ch, 0, QUANT_CHANNEL_COUNT - 1);
    q_engine[ch].Configure(scale, 0);
}

} // namespace HS
