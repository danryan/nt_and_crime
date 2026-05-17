#include <distingnt/api.h>
#include <new>
#include <cstring>

struct _busProbe : public _NT_algorithm {
    float testLevel;
};

enum { kParamOut, kParamOutMode, kParamLevel };
static const _NT_parameter parameters[] = {
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Out", 1, 13)
    { .name = "Level", .min = 0, .max = 100, .def = 50,
      .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
};
static const uint8_t page1[] = { kParamOut, kParamOutMode, kParamLevel };
static const _NT_parameterPage pages[] = {
    { .name = "Probe", .numParams = ARRAY_SIZE(page1), .params = page1 },
};
static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages), .pages = pages,
};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_busProbe);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                        const _NT_algorithmRequirements&, const int32_t*) {
    auto* alg = new (ptrs.sram) _busProbe();
    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    alg->testLevel      = 0.5f;
    return alg;
}

void parameterChanged(_NT_algorithm* self, int p) {
    auto* a = (_busProbe*)self;
    if (p == kParamLevel) a->testLevel = a->v[kParamLevel] / 100.0f;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a = (_busProbe*)self;
    int bus = a->v[kParamOut];
    if (bus < 1 || bus > kNT_lastBus) return;
    int numFrames = numFramesBy4 * 4;
    float* out = busFrames + (bus - 1) * numFrames;
    bool replace = a->v[kParamOutMode];
    if (replace) {
        for (int i = 0; i < numFrames; ++i) out[i] = a->testLevel;
    } else {
        for (int i = 0; i < numFrames; ++i) out[i] += a->testLevel;
    }
}

bool draw(_NT_algorithm* self) {
    auto* a = (_busProbe*)self;
    char buf[32];
    int len = NT_intToString(buf, a->v[kParamOut]);
    buf[len] = 0;
    NT_drawText(0, 30, "Probe bus:");
    NT_drawText(80, 30, buf);
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('P','r','o','B'),
    .name = "Bus probe",
    .description = "Writes a known level onto the selected output bus.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:     return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
