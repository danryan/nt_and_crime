#include "plugin_loader.h"
#include "nt_runtime.h"
#include <cstring>
#include <stdint.h>

// File-local state tracking calls to parameterChanged on the stub algorithm.
static int s_param_changed_count = 0;
static int s_last_changed_param  = -1;

// Storage for the single parameter value backed by the host.
// algorithm->v is const int16_t* in api.h; the host owns the backing store
// and performs a single const_cast when wiring it up in load_test_algorithm().
static int16_t s_param_storage[1] = { 0 };

// One parameter: "TestParam", range [0, 100], default 0, unit kNT_unitNone.
static const _NT_parameter s_stub_params[1] = {
    { "TestParam", 0, 100, 0, kNT_unitNone, kNT_scalingNone, nullptr }
};

static void stub_calculate_static_requirements(_NT_staticRequirements& req) {
    req.dram = 0;
}

static void stub_initialise(_NT_staticMemoryPtrs& ptrs, const _NT_staticRequirements& req) {
    (void)ptrs; (void)req;
}

static void stub_calculate_requirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    (void)specifications;
    req.numParameters = 1;
    req.sram = sizeof(_NT_algorithm);
    req.dram = 0;
    req.dtc  = 0;
    req.itc  = 0;
}

static _NT_algorithm* stub_construct(const _NT_algorithmMemoryPtrs& ptrs,
                                     const _NT_algorithmRequirements& req,
                                     const int32_t* specifications) {
    (void)req; (void)specifications;
    auto* alg = reinterpret_cast<_NT_algorithm*>(ptrs.sram);
    alg->parameters     = s_stub_params;
    alg->parameterPages = nullptr;
    // Wire the host-owned storage into the algorithm.
    // api.h declares v as const int16_t*; the host manages the backing store
    // and casts away const here — algorithms treat v[] as read-only, but the
    // host must be able to write it when NT_setParameterFromUi is called.
    const_cast<int16_t*&>(const_cast<int16_t*&>(alg->v)) = s_param_storage;
    return alg;
}

static void stub_parameter_changed(_NT_algorithm* self, int p) {
    (void)self;
    s_param_changed_count += 1;
    s_last_changed_param   = p;
}

static void stub_step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    (void)self; (void)busFrames; (void)numFramesBy4;
}

static bool stub_draw(_NT_algorithm* self) {
    (void)self;
    return false;
}

static const _NT_factory s_stub_factory = {
    NT_MULTICHAR('T','S','T','B'),
    "TestStub",
    "Stub algorithm for parameter tests",
    0,
    nullptr,
    stub_calculate_static_requirements,
    stub_initialise,
    stub_calculate_requirements,
    stub_construct,
    stub_parameter_changed,
    stub_step,
    stub_draw,
    nullptr, // midiRealtime
    nullptr, // midiMessage
    0,       // tags
    nullptr, // hasCustomUi
    nullptr, // customUi
    nullptr, // setupUi
    nullptr, // serialise
    nullptr, // deserialise
    nullptr, // midiSysEx
    nullptr, // parameterUiPrefix
    nullptr, // parameterString
};

// Singleton backing store for the stub algorithm struct.
// Sized to hold _NT_algorithm by value; construct() writes into it.
static uint8_t s_algorithm_sram[sizeof(_NT_algorithm)];

static nt::LoadedPlugin s_plugin = { nullptr, nullptr };

namespace nt {

void reset_plugin_loader() {
    s_param_changed_count = 0;
    s_last_changed_param  = -1;
    s_param_storage[0]    = 0;
    // The singleton is re-registered on the next load_test_algorithm() call.
    s_plugin.factory   = nullptr;
    s_plugin.algorithm = nullptr;
}

LoadedPlugin* load_test_algorithm() {
    if (s_plugin.algorithm != nullptr) {
        return &s_plugin;
    }

    _NT_staticRequirements sreq;
    stub_calculate_static_requirements(sreq);

    _NT_staticMemoryPtrs sptrs;
    sptrs.dram = nullptr;
    stub_initialise(sptrs, sreq);

    _NT_algorithmRequirements areq;
    stub_calculate_requirements(areq, nullptr);

    _NT_algorithmMemoryPtrs aptrs;
    aptrs.sram = s_algorithm_sram;
    aptrs.dram = nullptr;
    aptrs.dtc  = nullptr;
    aptrs.itc  = nullptr;

    _NT_algorithm* alg = stub_construct(aptrs, areq, nullptr);

    s_plugin.factory   = &s_stub_factory;
    s_plugin.algorithm = alg;

    // Register into the single harness slot so NT_algorithmIndex() can find it.
    nt::register_algorithm(&s_plugin);

    return &s_plugin;
}

int test_parameter_changed_count() {
    return s_param_changed_count;
}

int test_last_changed_param() {
    return s_last_changed_param;
}

} // namespace nt
