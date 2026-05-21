// ReentrancyProbe: diagnostic for NT_setParameterFromUi reentrancy when
// called from parameterChanged. Two parameters A and B. When the user
// turns the L encoder one click, customUi calls NT_setParameterFromUi for
// param A; firmware fires parameterChanged(0); the body of pc0 calls
// NT_setParameterFromUi for param B; firmware fires parameterChanged(1).
//
// Counters captured on the instance:
//   pc0_calls          : total parameterChanged(0) firings.
//   pc1_calls          : total parameterChanged(1) firings.
//   pc1_during_pc0     : parameterChanged(1) firings observed while pc0
//                        was still on the call stack.
//   pc0_in_flight      : guard flag toggled around the inner setter.
//
// Hardware interpretation:
//   NEST = pc1_during_pc0
//   NEST > 0                  => synchronous reentry from firmware; host
//                                proxy MUST install a re-entry guard.
//   NEST == 0 && PC1 > 0      => firmware defers pc1 to a later frame;
//                                guard is harmless but unnecessary.
//   PC1 == 0                  => NT_setParameterFromUi did not deliver
//                                a parameterChanged(1); abort and post
//                                report. Check NT_parameterOffset usage.
//
// Build: make arm -> build/arm/reentrancy_probe.o
// Deploy: make deploy-sysex SYSEX_PLUGIN=build/arm/reentrancy_probe.o SYSEX_ID=<free slot>
//
// Spec: docs/superpowers/specs/2026-05-21-host-ux-rework-design.md
// Kickoff: docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md

#include <distingnt/api.h>
#include <new>
#include <cstdint>
#include <cstddef>

struct _ReentrancyProbe : public _NT_algorithm {
    uint32_t pc0_calls;
    uint32_t pc1_calls;
    uint32_t pc1_during_pc0;
    bool     pc0_in_flight;
};

enum { kParamA, kParamB, kParamCount };

static const _NT_parameter parameters[] = {
    { .name = "A", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "B", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
};

static const uint8_t page1[] = { kParamA, kParamB };
static const _NT_parameterPage pages[] = {
    { .name = "Probe", .numParams = ARRAY_SIZE(page1), .params = page1 },
};
static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages), .pages = pages,
};

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = kParamCount;
    req.sram = sizeof(_ReentrancyProbe);
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                const _NT_algorithmRequirements&,
                                const int32_t*) {
    auto* alg = new (ptrs.sram) _ReentrancyProbe();
    alg->parameters      = parameters;
    alg->parameterPages  = &parameterPages;
    alg->pc0_calls       = 0;
    alg->pc1_calls       = 0;
    alg->pc1_during_pc0  = 0;
    alg->pc0_in_flight   = false;
    return alg;
}

static void parameterChanged(_NT_algorithm* self, int p) {
    auto* a = (_ReentrancyProbe*)self;
    if (p == kParamA) {
        a->pc0_calls++;
        a->pc0_in_flight = true;
        // Forward to self's param B. Use the documented self-call form:
        // global index = local + NT_parameterOffset().
        int16_t next = (int16_t)(a->v[kParamA]);
        NT_setParameterFromUi((uint32_t)NT_algorithmIndex(self),
                              (uint32_t)(kParamB + NT_parameterOffset()),
                              next);
        a->pc0_in_flight = false;
    } else if (p == kParamB) {
        a->pc1_calls++;
        if (a->pc0_in_flight) {
            a->pc1_during_pc0++;
        }
    }
}

static void step(_NT_algorithm* /*self*/, float* /*busFrames*/, int /*numFramesBy4*/) {
}

static bool draw(_NT_algorithm* self) {
    auto* a = (_ReentrancyProbe*)self;
    char buf[32];
    NT_drawText(0, 10, "ReentrancyProbe");

    NT_drawText(0, 25, "PC0=");
    int len = NT_intToString(buf, (int)a->pc0_calls);
    buf[len] = 0;
    NT_drawText(32, 25, buf);

    NT_drawText(64, 25, "PC1=");
    len = NT_intToString(buf, (int)a->pc1_calls);
    buf[len] = 0;
    NT_drawText(96, 25, buf);

    NT_drawText(0, 40, "NEST=");
    len = NT_intToString(buf, (int)a->pc1_during_pc0);
    buf[len] = 0;
    NT_drawText(40, 40, buf);

    NT_drawText(0, 55, "A=");
    len = NT_intToString(buf, (int)a->v[kParamA]);
    buf[len] = 0;
    NT_drawText(16, 55, buf);

    NT_drawText(64, 55, "B=");
    len = NT_intToString(buf, (int)a->v[kParamB]);
    buf[len] = 0;
    NT_drawText(80, 55, buf);

    return true;
}

static uint32_t hasCustomUi(_NT_algorithm* /*self*/) {
    return kNT_encoderL;
}

static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
    if (data.encoders[0] != 0) {
        int16_t next = (int16_t)(self->v[kParamA] + data.encoders[0]);
        if (next < 0) next = 0;
        if (next > 1000) next = 1000;
        NT_setParameterFromUi((uint32_t)NT_algorithmIndex(self),
                              (uint32_t)(kParamA + NT_parameterOffset()),
                              next);
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('R','e','P','r'),
    .name = "ReentrancyProbe",
    .description = "Diagnostic: NT_setParameterFromUi reentrancy from parameterChanged.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
    .hasCustomUi = hasCustomUi,
    .customUi = customUi,
};

extern "C" __attribute__((visibility("default"))) uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
