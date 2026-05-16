#include "plugin_loader.h"
#include "nt_runtime.h"
#include <cstring>
#include <stdint.h>
#include <vector>

// Weak fallback for test binaries that do not link a real plug-in.
// When a real plug-in's .cpp is linked its pluginEntry() takes precedence;
// otherwise load_plugin() sees a null factory and returns nullptr safely.
extern "C" __attribute__((weak))
uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    (void)selector; (void)data;
    return 0;
}

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

// ---- load_plugin() singleton state ----
// Memory regions for the real statically linked plug-in.
// Held in std::vector<uint8_t> so they outlive the loader call.
struct RealPluginStore {
    std::vector<uint8_t> sram;
    std::vector<uint8_t> dram;
    std::vector<uint8_t> dtc;
    std::vector<uint8_t> itc;
    std::vector<uint8_t> static_dram;
    std::vector<int16_t> v;
    nt::LoadedPlugin plugin;
};

static RealPluginStore s_real_store;
static bool s_real_loaded = false;

namespace nt {

void reset_plugin_loader() {
    s_param_changed_count = 0;
    s_last_changed_param  = -1;
    s_param_storage[0]    = 0;
    // The singleton is re-registered on the next load_test_algorithm() call.
    s_plugin.factory   = nullptr;
    s_plugin.algorithm = nullptr;
    // Reset the real plugin singleton so it can be re-loaded cleanly.
    s_real_loaded = false;
    s_real_store = RealPluginStore{};
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

LoadedPlugin* load_plugin() {
    if (s_real_loaded) {
        return &s_real_store.plugin;
    }

    // Step 1: obtain the factory from the statically linked plug-in.
    uintptr_t raw = pluginEntry(kNT_selector_factoryInfo, 0);
    const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(raw);
    if (!factory) {
        return nullptr;
    }

    // Step 2: calculateStaticRequirements + initialise (both optional).
    if (factory->calculateStaticRequirements) {
        _NT_staticRequirements sreq;
        sreq.dram = 0;
        factory->calculateStaticRequirements(sreq);
        if (sreq.dram > 0) {
            s_real_store.static_dram.assign(sreq.dram, 0);
        }
        if (factory->initialise) {
            _NT_staticMemoryPtrs sptrs;
            sptrs.dram = s_real_store.static_dram.empty() ? nullptr : s_real_store.static_dram.data();
            factory->initialise(sptrs, sreq);
        }
    }

    // Step 3: calculateRequirements.
    _NT_algorithmRequirements areq;
    areq.numParameters = 0;
    areq.sram = 0;
    areq.dram = 0;
    areq.dtc  = 0;
    areq.itc  = 0;
    factory->calculateRequirements(areq, nullptr);

    // Step 4: allocate the four memory regions.
    // sram must be large enough for the algorithm struct (at minimum sizeof(_NT_algorithm));
    // gainCustomUI uses placement-new of _gainAlgorithm which is larger.
    uint32_t sram_size = areq.sram > 0 ? areq.sram : static_cast<uint32_t>(sizeof(_NT_algorithm));
    s_real_store.sram.assign(sram_size, 0);
    if (areq.dram > 0) { s_real_store.dram.assign(areq.dram, 0); }
    if (areq.dtc  > 0) { s_real_store.dtc.assign(areq.dtc,   0); }
    if (areq.itc  > 0) { s_real_store.itc.assign(areq.itc,   0); }

    _NT_algorithmMemoryPtrs aptrs;
    aptrs.sram = s_real_store.sram.data();
    aptrs.dram = s_real_store.dram.empty() ? nullptr : s_real_store.dram.data();
    aptrs.dtc  = s_real_store.dtc.empty()  ? nullptr : s_real_store.dtc.data();
    aptrs.itc  = s_real_store.itc.empty()  ? nullptr : s_real_store.itc.data();

    // Step 5: construct the algorithm instance.
    _NT_algorithm* alg = factory->construct(aptrs, areq, nullptr);
    if (!alg) {
        return nullptr;
    }

    // Step 6: allocate v[] and wire it into the algorithm.
    // algorithm->v is const int16_t*; the host owns the backing store.
    // Default each parameter value to its declared def.
    uint32_t num_params = areq.numParameters;
    s_real_store.v.assign(num_params, 0);
    for (uint32_t i = 0; i < num_params; ++i) {
        s_real_store.v[i] = alg->parameters ? alg->parameters[i].def : 0;
    }
    const_cast<int16_t*&>(alg->v) = s_real_store.v.data();

    // Step 7: populate the LoadedPlugin and register it.
    s_real_store.plugin.factory   = factory;
    s_real_store.plugin.algorithm = alg;

    nt::register_algorithm(&s_real_store.plugin);

    s_real_loaded = true;
    return &s_real_store.plugin;
}

int test_parameter_changed_count() {
    return s_param_changed_count;
}

int test_last_changed_param() {
    return s_last_changed_param;
}

} // namespace nt
