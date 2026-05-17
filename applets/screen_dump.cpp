#include <distingnt/api.h>
#include <new>
#include "hem_dump_helper.h"

struct _screenDump : public _NT_algorithm { };

static const _NT_parameter parameters[] = {};
static const _NT_parameterPages parameterPages = { .numPages = 0, .pages = nullptr };

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = 0;
    req.sram = sizeof(_screenDump);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                        const _NT_algorithmRequirements&, const int32_t*) {
    auto* alg = new (ptrs.sram) _screenDump();
    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm*, float*, int) {}

bool draw(_NT_algorithm*) {
    nt_hem::_nt_hem_dump_screen();
    return true;  // suppress default param line
}

void midiSysEx(const uint8_t* message, uint32_t count) {
    // Diagnostic: always echo a sentinel on ANY inbound SysEx so we can
    // distinguish midiSysEx-fires from NT_sendMidiSysEx-routes-out.
    // Reply pattern: F0 7D 02 42 F7
    uint8_t sentinel[3] = { 0x7D, 0x02, 0x42 };
    NT_sendMidiSysEx(kNT_destinationUSB, sentinel, sizeof(sentinel), true);

    if (count >= 2 && message[0] == nt_hem::kManufacturerId
                   && message[1] == nt_hem::kCmdDumpRequest) {
        nt_hem::emit_capture_if_pending();
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('D','m','p','S'),
    .name = "Screen dump",
    .description = "Co-load in slot 2; emits NT_screen as SysEx on request.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
    .midiSysEx = midiSysEx,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:     return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
