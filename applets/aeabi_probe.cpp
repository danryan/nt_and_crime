// AeabiProbe: minimal diagnostic to determine if NT firmware provides
// __aeabi_ldivmod / __aeabi_uldivmod. No new/delete, no memmove, no
// std::function. If this loads cleanly the libgcc int64 helpers are
// available; if not, the device error names the first missing symbol.
//
// Build: included by `make arm` via applets/aeabi_probe.cpp -> build/arm/aeabi_probe.o
// Deploy: make deploy-sysex SYSEX_PLUGIN=build/arm/aeabi_probe.o SYSEX_ID=<free slot>

#include <distingnt/api.h>
#include <new>
#include <cstdint>
#include <cstddef>

struct _aeabiProbe : public _NT_algorithm {
    int64_t  i64_result;
    uint64_t u64_result;
};

enum { kParamA, kParamB, kParamCount };
static const _NT_parameter parameters[] = {
    { .name = "A", .min = 1, .max = 1000, .def = 1000, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "B", .min = 1, .max = 100,  .def = 7,    .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
};
static const uint8_t page1[] = { kParamA, kParamB };
static const _NT_parameterPage pages[] = {
    { .name = "Probe", .numParams = ARRAY_SIZE(page1), .params = page1 },
};
static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages), .pages = pages,
};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = kParamCount;
    req.sram = sizeof(_aeabiProbe);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                        const _NT_algorithmRequirements&, const int32_t*) {
    auto* alg = new (ptrs.sram) _aeabiProbe();  // placement-new, no allocator
    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    alg->i64_result = 0;
    alg->u64_result = 0;
    return alg;
}

// Volatile keeps the divides from being constant-folded.
static volatile int64_t  vol_i64_num = 1000000000;
static volatile int64_t  vol_i64_den = 7;
static volatile uint64_t vol_u64_num = 1000000000ULL;
static volatile uint64_t vol_u64_den = 7ULL;

void step(_NT_algorithm* self, float* /*busFrames*/, int /*numFramesBy4*/) {
    auto* a = (_aeabiProbe*)self;
    int64_t  inum = vol_i64_num * (int64_t)a->v[kParamA];
    int64_t  iden = vol_i64_den + (int64_t)a->v[kParamB];
    a->i64_result = inum / iden;  // emits __aeabi_ldivmod
    uint64_t unum = vol_u64_num * (uint64_t)a->v[kParamA];
    uint64_t uden = vol_u64_den + (uint64_t)a->v[kParamB];
    a->u64_result = unum / uden;  // emits __aeabi_uldivmod
}

bool draw(_NT_algorithm* self) {
    auto* a = (_aeabiProbe*)self;
    char buf[32];
    NT_drawText(0, 10, "AeabiProbe");

    NT_drawText(0, 25, "i64 lo:");
    int len = NT_intToString(buf, (int)(a->i64_result & 0x7fffffff));
    buf[len] = 0;
    NT_drawText(64, 25, buf);

    NT_drawText(0, 40, "u64 lo:");
    len = NT_intToString(buf, (int)(a->u64_result & 0x7fffffff));
    buf[len] = 0;
    NT_drawText(64, 40, buf);
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('A','e','P','r'),
    .name = "AeabiProbe",
    .description = "Diagnostic: int64 divide via __aeabi_ldivmod/__aeabi_uldivmod.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
