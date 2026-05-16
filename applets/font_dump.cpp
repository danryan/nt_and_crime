#include <distingnt/api.h>
#include <new>
#include <cstring>
#include "hem_dump_helper.h"

struct _fontDump : public _NT_algorithm {
    int      glyph_index;  // 0..94
    int      font_size;    // 0,1,2 -> tiny,normal,large
    uint32_t frame_counter;
};

static const char* kSizeEnum[] = { "tiny", "normal", "large" };

static const _NT_parameter parameters[] = {
    { .name = "Size", .min = 0, .max = 2, .def = 1,
      .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kSizeEnum },
};
static const uint8_t page1[] = { 0 };
static const _NT_parameterPage pages[] = {
    { .name = "FontDump", .numParams = 1, .params = page1 },
};
static const _NT_parameterPages parameterPages = { .numPages = 1, .pages = pages };

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_fontDump);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                        const _NT_algorithmRequirements&, const int32_t*) {
    auto* alg = new (ptrs.sram) _fontDump();
    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    alg->glyph_index = 0;
    alg->font_size   = 1;
    return alg;
}

void parameterChanged(_NT_algorithm* self, int) {
    auto* a = (_fontDump*)self;
    a->font_size    = a->v[0];
    a->glyph_index  = 0;
}

void step(_NT_algorithm* self, float*, int) {
    auto* a = (_fontDump*)self;
    ++a->frame_counter;
}

bool draw(_NT_algorithm* self) {
    auto* a = (_fontDump*)self;
    std::memset(NT_screen, 0, sizeof(NT_screen));
    char ch = (char)(32 + a->glyph_index);
    char str[2] = { ch, 0 };
    _NT_textSize sz = (a->font_size == 0) ? kNT_textTiny
                    : (a->font_size == 1) ? kNT_textNormal : kNT_textLarge;
    NT_drawText(0, 0, str, 15, kNT_textLeft, sz);
    nt_hem::_nt_hem_dump_screen();
    a->glyph_index = (a->glyph_index + 1) % 95;
    return true;
}

void midiSysEx(const uint8_t* message, uint32_t count) {
    if (count >= 2 && message[0] == nt_hem::kManufacturerId
                   && message[1] == nt_hem::kCmdDumpRequest) {
        nt_hem::emit_capture_if_pending();
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('D','m','p','F'),
    .name = "Font dump",
    .description = "Iterates ASCII glyphs and exposes them via screen_dump SysEx.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
    .midiSysEx = midiSysEx,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
