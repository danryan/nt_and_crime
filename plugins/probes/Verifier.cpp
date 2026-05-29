#include <distingnt/api.h>
#include <new>
#include <cstddef>
#include "verifier_logic.h"

using namespace verifier;

enum { kP_View, kP_First, kP_Count, kP_Mode, kP_Reset, kP_ScopeBus, kP_Timebase };
enum { kReset_Off = 0, kReset_On = 1 };

struct _verifier : public _NT_algorithm {
    Reduction red[kMaxBuses];
    float     scope[kScopeWidth];
    int       scope_wr;
    int       scope_phase;
    bool      scope_filled;
};

static char const* const viewStrings[]  = { "Numeric", "Scope" };
static char const* const modeStrings[]  = { "Mean", "Min", "Max", "PkPk" };
static char const* const resetStrings[] = { "Off", "Reset" };

static const _NT_parameter parameters[] = {
    { .name = "View",      .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = viewStrings },
    { .name = "First bus", .min = 1, .max = kNT_lastBus, .def = 13, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Count",     .min = 1, .max = kMaxBuses, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Mode",      .min = 0, .max = 3, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = modeStrings },
    { .name = "Reset",     .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = resetStrings },
    { .name = "Scope bus", .min = 1, .max = kNT_lastBus, .def = 13, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Timebase",  .min = 1, .max = 64, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
};
static const uint8_t page1[] = { kP_View, kP_First, kP_Count, kP_Mode, kP_Reset, kP_ScopeBus, kP_Timebase };
static const _NT_parameterPage pages[] = {
    { .name = "Verifier", .numParams = ARRAY_SIZE(page1), .params = page1 },
};
static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages), .pages = pages,
};

static void clear_accumulators(_verifier* a) {
    for (int i = 0; i < kMaxBuses; ++i) reduction_reset(a->red[i]);
    a->scope_wr = 0; a->scope_phase = 0; a->scope_filled = false;
}

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_verifier);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements&, const int32_t*) {
    auto* a = new (ptrs.sram) _verifier();
    a->parameters     = parameters;
    a->parameterPages = &parameterPages;
    clear_accumulators(a);
    return a;
}

void parameterChanged(_NT_algorithm* self, int p) {
    auto* a = (_verifier*)self;
    if (p == kP_Reset && a->v[kP_Reset] == kReset_On) clear_accumulators(a);
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a = (_verifier*)self;
    int numFrames = numFramesBy4 * 4;
    int first = a->v[kP_First];
    int count = a->v[kP_Count];
    if (count > kMaxBuses) count = kMaxBuses;
    for (int i = 0; i < count; ++i) {
        int bus = first + i;
        if (bus < 1 || bus > kNT_lastBus) continue;
        const float* in = busFrames + (bus - 1) * numFrames;
        reduction_accumulate(a->red[i], in, numFrames);
    }
    int sbus = a->v[kP_ScopeBus];
    if (sbus >= 1 && sbus <= kNT_lastBus) {
        const float* sin = busFrames + (sbus - 1) * numFrames;
        scope_push(a->scope, a->scope_wr, a->scope_phase, a->scope_filled,
                   sin, numFrames, a->v[kP_Timebase]);
    }
}

bool draw(_NT_algorithm* self) {
    auto* a = (_verifier*)self;
    if (a->v[kP_View] == kNumeric) {
        int count = a->v[kP_Count];
        if (count > kMaxBuses) count = kMaxBuses;
        int   buses[kMaxBuses];
        float values[kMaxBuses];
        for (int i = 0; i < count; ++i) {
            buses[i]  = a->v[kP_First] + i;
            values[i] = reduction_value(a->red[i], a->v[kP_Mode]);
        }
        render_numeric(buses, values, count);
    } else {
        int trig = scope_trigger(a->scope, kScopeWidth);
        render_scope(a->scope, kScopeWidth, trig, 5.0f);
    }
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('V','r','f','y'),
    .name = "Verifier",
    .description = "Reads any bus and renders a screenshot-parseable readout.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
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

// Host-test accessor: the mean of accumulator `row`.
float verifier_mean_for_test(_NT_algorithm* self, int row) {
    auto* a = (_verifier*)self;
    return reduction_value(a->red[row], verifier::kMean);
}
