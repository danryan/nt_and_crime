#include <distingnt/api.h>
#include <new>
#include <cstring>

struct _busProbe : public _NT_algorithm {
    int targetBus;
    float testLevel;
};

enum { kParamBus, kParamLevel };
static const _NT_parameter parameters[] = {
    { .name = "Bus",   .min = 1, .max = kNT_lastBus, .def = 1,
      .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Level", .min = 0, .max = 100, .def = 50,
      .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
};
static const uint8_t page1[] = { kParamBus, kParamLevel };
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
    alg->targetBus      = 1;
    alg->testLevel      = 0.5f;
    return alg;
}

void parameterChanged(_NT_algorithm* self, int p) {
    auto* a = (_busProbe*)self;
    if (p == kParamBus)   a->targetBus = a->v[kParamBus];
    if (p == kParamLevel) a->testLevel = a->v[kParamLevel] / 100.0f;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a = (_busProbe*)self;
    int numFrames = numFramesBy4 * 4;
    float* out = busFrames + (a->targetBus - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) out[i] = a->testLevel;
}

bool draw(_NT_algorithm* self) {
    auto* a = (_busProbe*)self;
    char buf[32];
    int len = NT_intToString(buf, a->targetBus);
    buf[len] = 0;
    NT_drawText(0, 30, "Probe bus:");
    NT_drawText(80, 30, buf);
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('P','r','o','B'),
    .name = "Bus probe",
    .description = "Writes a known level onto the selected bus.",
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
