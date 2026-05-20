// SectionProbe: diagnostic that tests whether the NT firmware loader honors
// custom section attributes that route code away from the canonical .text section.
//
// If the loader places .code_dram in some RAM region and resolves calls to it:
//   draw() shows result matching the expected value computed in dram_fn.
//
// If the loader ignores the section (drops it or fails to map it):
//   step() call into probe_dram_fn hits unmapped memory -> hardware fault.
//
// If the loader rejects the plug-in entirely:
//   Misc > Plug-ins > View Info marks SectionProbe Failed with reason.
//
// Build: make build/arm/section_probe.o (rule mirrors aeabi_probe).
// Deploy: make deploy-sysex SYSEX_PLUGIN=build/arm/section_probe.o SYSEX_ID=<free slot>

#include <distingnt/api.h>
#include <new>
#include <cstdint>
#include <cstddef>

// Force into a non-canonical section. `used` prevents -Os from eliminating
// the function. `noinline` ensures the body actually lives in .code_dram
// rather than being inlined into step()'s .text.
extern "C" __attribute__((section(".code_dram"), noinline, used))
uint32_t probe_dram_fn(uint32_t x) {
    return x * 13u + 0xDEADBEEFu;
}

struct _SectionProbe : public _NT_algorithm {
    uint32_t result;
};

enum { kParamX, kParamCount };
static const _NT_parameter parameters[] = {
    { .name = "X", .min = 0, .max = 1000, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
};
static const uint8_t page1[] = { kParamX };
static const _NT_parameterPage pages[] = {
    { .name = "Probe", .numParams = ARRAY_SIZE(page1), .params = page1 },
};
static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages), .pages = pages,
};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = kParamCount;
    req.sram = sizeof(_SectionProbe);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements&, const int32_t*) {
    auto* a = new (ptrs.sram) _SectionProbe();
    a->parameters     = parameters;
    a->parameterPages = &parameterPages;
    a->result = 0;
    return a;
}

void step(_NT_algorithm* self, float* /*busFrames*/, int /*numFramesBy4*/) {
    auto* a = (_SectionProbe*)self;
    a->result = probe_dram_fn((uint32_t)a->v[kParamX]);
}

bool draw(_NT_algorithm* self) {
    auto* a = (_SectionProbe*)self;
    char buf[32];

    NT_drawText(0, 10, "SectionProbe");

    NT_drawText(0, 25, "got:");
    int len = NT_intToString(buf, (int)(a->result & 0x7fffffff));
    buf[len] = 0;
    NT_drawText(40, 25, buf);

    uint32_t expected = ((uint32_t)a->v[kParamX]) * 13u + 0xDEADBEEFu;
    NT_drawText(0, 40, "exp:");
    len = NT_intToString(buf, (int)(expected & 0x7fffffff));
    buf[len] = 0;
    NT_drawText(40, 40, buf);
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('S','c','P','r'),
    .name = "SectionProbe",
    .description = "Tests if loader honors __attribute__((section(\".code_dram\"))).",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
};

extern "C" __attribute__((visibility("default"))) uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
