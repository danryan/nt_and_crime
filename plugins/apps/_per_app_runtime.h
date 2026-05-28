#pragma once

// Aggregation trigger. A per-app plug-in TU (plugins/apps/<APP>.cpp) defines
// NT_OC_APP_TU before including this header; that pulls the OC shim impl
// aggregation (all the OC-specific shim .cpp bodies) into the single per-app
// TU, exactly as hem_shim.h does for Hemisphere applets (hem_shim.h:3-6). The
// guard inside oc_shim_impl.h (NT_HEM_NO_IMPL) makes the include idempotent.
//
// Host tests that link the shim sources separately (test_oc_runtime,
// test_oc_router) and harness helpers (oc_ui_sim.h) include this header
// WITHOUT defining NT_OC_APP_TU, so they never aggregate and never collide
// with their separately-linked SHIM_CORE_SRCS / oc/io.cpp. The per-app host
// test (test_oc_app_<APP>) likewise does not define NT_OC_APP_TU: the
// aggregating per-app .cpp it links supplies every shim symbol, so the test TU
// must stay non-aggregating to avoid duplicate definitions.
#if defined(NT_OC_APP_TU)
#include "../../shim/include/oc_shim_impl.h"
#endif

// Per-app runtime for O_C full-screen apps ported as disting NT plug-ins. The
// header is included once by each plugins/apps/<APP>.cpp. It provides:
//
//   * AppAlgorithm: a fixed-capacity _NT_algorithm subclass carrying the
//     embedded OC::App pointer, the type-erased settings facade, the parameter
//     table (I/O routing plus per-setting), the parameter value storage, the
//     tick accumulator, and the construct-time sentinel.
//   * oc_runtime::construct(...): builds the parameter table (12 I/O routing
//     entries plus one per app setting), wires the SettingsFacade, sets
//     OC::apps::current_app, fires HandleAppEvent(APP_EVENT_RESUME), and seeds
//     the v[] storage from each parameter's default.
//   * oc_runtime::step(busFrames, ...): tick-accumulator-driven isr() dispatch
//     with the one-edge-per-tick discipline (DigitalInputs::Scan runs once per
//     tick; the rising-edge latch only fires on low->high transitions). Before
//     the isr ticks it refreshes the OC input backing (OC::ADC and the trigger
//     store) from the routed CV-in / TR-in buses, sampled once per buffer;
//     after the ticks it flushes OC::DAC onto the routed CV-out buses. The
//     bus selections come from the 12 I/O routing params (v[]). A null-bus
//     overload skips routing for the cadence/edge-only tests. Increments
//     OC::CORE::ticks per tick (the Hemisphere runtime does this at
//     _per_applet_runtime.h:203; standalone O_C apps must do it themselves).
//   * oc_runtime::draw(...): increments the idle counter, calls loop() and
//     DrawMenu()/DrawScreensaver() into [0,128), then row-shifts NT_screen
//     right by 32 bytes (= 64 pixels) into [64,192) and blanks the margins.
//     After the first draw(), arms the construct-time sentinel.
//   * oc_runtime::parameterChanged(alg, idx): guarded by the sentinel. Routes
//     idx to either the I/O routing block or the settings facade. App-side
//     edits push values back via NT_setParameterFromUi (also guarded).
//   * oc_runtime::serialise/deserialise: wrap SettingsBase::Save/Restore into
//     a single base64-encoded "blob" JSON member, following the precedent in
//     _per_applet_runtime.h's "hemi_hi"/"hemi_lo" pair.
//
// The runtime is templated on the typed SettingsBase subclass (so it can read
// value_attr_) but exposes a non-template AppAlgorithm whose facade is set up
// via a template construct() call. Tests construct an AppAlgorithm directly;
// the per-app .cpp wires construct() through the factory's construct() thunk.

#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <cstdint>
#include <cstring>
#include <new>

#include "OC_apps.h"
#include "OC_ui.h"
#include "OC_core.h"
#include "OC_config.h"
#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_digital_inputs.h"
#include "Arduino.h"
#include "util/util_settings.h"

namespace oc_runtime {

// ---------------------------------------------------------------------------
// Capacities and layout
// ---------------------------------------------------------------------------

// I/O routing block: 4 CV inputs + 4 CV outputs + 4 trigger inputs. The
// CV-output slots also accept an "output mode" (replace vs add); the
// foundation collapses this to a fixed replace-mode default to keep the
// parameter table flat at 12 entries. If a later app needs per-output mode,
// add 4 more entries and a per-output mode index.
constexpr int kNumCvInputs   = 4;
constexpr int kNumCvOutputs  = 4;
constexpr int kNumTrigInputs = 4;
constexpr int kIoParamCount  = kNumCvInputs + kNumCvOutputs + kNumTrigInputs;

// Maximum app settings supported (Harrington1200 stores 37; cover headroom).
constexpr int kMaxSettings = 64;

// Total maximum parameter count.
constexpr int kMaxParams = kIoParamCount + kMaxSettings;

// Settings-blob upper bound (Harrington1200 packs 37 settings into ~80
// bytes worst-case). The base64-encoded JSON member adds ~33% overhead.
constexpr int kMaxBlobBytes = 256;
constexpr int kMaxBlobB64   = ((kMaxBlobBytes + 2) / 3) * 4 + 1;

inline int settings_param_base() { return kIoParamCount; }

// ---------------------------------------------------------------------------
// Settings facade
// ---------------------------------------------------------------------------
//
// Type-erased adapter onto the typed SettingsBase<clazz, num_settings>
// instance. Each pointer-to-function captures a typed call site so the runtime
// can read value_attr_, get/set values, and serialise without seeing the
// concrete class.
struct SettingsFacade {
    void* instance = nullptr;
    int   num_settings = 0;
    int   (*get_value)(void* self, int idx) = nullptr;
    bool  (*apply_value)(void* self, int idx, int value) = nullptr;
    size_t (*save)(void* self, void* blob) = nullptr;
    size_t (*restore)(void* self, const void* blob) = nullptr;
    size_t (*storage_size)() = nullptr;
    const settings::value_attr* (*value_attr_at)(int idx) = nullptr;
};

template <typename Settings>
SettingsFacade make_facade(Settings* s) {
    SettingsFacade f;
    f.instance     = s;
    // num_settings is the SettingsBase<clazz, N> template arg and cannot be
    // recovered from a non-pseudo-static accessor without naming it. The
    // caller of construct() passes it explicitly and overwrites this field.
    f.num_settings = 0;
    f.get_value   = [](void* self, int idx) -> int {
        return static_cast<Settings*>(self)->get_value(static_cast<size_t>(idx));
    };
    f.apply_value = [](void* self, int idx, int value) -> bool {
        return static_cast<Settings*>(self)->apply_value(static_cast<size_t>(idx), value);
    };
    f.save = [](void* self, void* blob) -> size_t {
        return static_cast<Settings*>(self)->Save(blob);
    };
    f.restore = [](void* self, const void* blob) -> size_t {
        return static_cast<Settings*>(self)->Restore(blob);
    };
    f.storage_size = []() -> size_t {
        return Settings::storageSize();
    };
    f.value_attr_at = [](int idx) -> const settings::value_attr* {
        return &Settings::value_attr(static_cast<size_t>(idx));
    };
    return f;
}

// ---------------------------------------------------------------------------
// AppAlgorithm
// ---------------------------------------------------------------------------
//
// One AppAlgorithm per app instance. Lives in the algorithm SRAM region the
// firmware hands to construct(). For host tests it lives on the stack and is
// also a valid _NT_algorithm subclass.
struct AppAlgorithm : public _NT_algorithm {
    const OC::App* app = nullptr;
    SettingsFacade settings_facade;

    // Parameter table. Built once per construct(). The base entries (0..11)
    // are I/O routing; entries [12, 12 + num_settings) are app settings.
    _NT_parameter parameters_storage[kMaxParams] = {};

    // Backing for _NT_algorithm::v (declared const int16_t* in api.h). We
    // own the storage and const_cast on wire-up.
    int16_t v_storage[kMaxParams] = {};

    // Tick accumulator. Each step adds (numFrames * OC_CORE_ISR_FREQ) to the
    // numerator; while numerator >= sample_rate we emit one isr tick and
    // subtract sample_rate. This keeps the long-term tick rate equal to
    // OC_CORE_ISR_FREQ (~16.666 kHz) regardless of NT's sample rate.
    uint64_t tick_numerator = 0;

    // Idle counter for screensaver transitions, reset by any control event.
    uint32_t idle_count = 0;

    // Construct-time sentinel. Firmware fires parameterChanged for every
    // parameter during construct (before NT_algorithmIndex is valid) and that
    // path MUST NOT touch app settings. The flag flips to true on the first
    // draw(), which is also when NT_setParameterFromUi push-backs are armed.
    bool alive = false;

    // Encoder push-edge state for the customUi router. Tracks the previous
    // controls mask so the router can detect transitions across calls.
    uint16_t last_controls = 0;
    // Held-since tick counters for long-press detection. Indexed by the bit
    // position of the kNT_* control constant.
    uint64_t held_since[16] = {};
};

// ---------------------------------------------------------------------------
// I/O routing parameter rows
// ---------------------------------------------------------------------------
//
// The 12 routing rows are emitted in a fixed order so settings start at
// kIoParamCount = 12. Defaults pick the first N input/output/aux buses so a
// freshly-loaded plug-in routes sensibly without any user action.
inline void emit_io_params(_NT_parameter* dst) {
    static const char* const cv_in_names[kNumCvInputs] = {
        "CV in 1", "CV in 2", "CV in 3", "CV in 4"
    };
    static const char* const cv_out_names[kNumCvOutputs] = {
        "CV out 1", "CV out 2", "CV out 3", "CV out 4"
    };
    static const char* const trig_in_names[kNumTrigInputs] = {
        "TR in 1", "TR in 2", "TR in 3", "TR in 4"
    };
    int idx = 0;
    for (int i = 0; i < kNumCvInputs; ++i) {
        dst[idx++] = _NT_parameter{
            cv_in_names[i], 0, kNT_lastBus,
            static_cast<int16_t>(1 + i),
            kNT_unitCvInput, 0, nullptr
        };
    }
    for (int i = 0; i < kNumCvOutputs; ++i) {
        dst[idx++] = _NT_parameter{
            cv_out_names[i], 0, kNT_lastBus,
            static_cast<int16_t>(13 + i),
            kNT_unitCvOutput, 0, nullptr
        };
    }
    for (int i = 0; i < kNumTrigInputs; ++i) {
        dst[idx++] = _NT_parameter{
            trig_in_names[i], 0, kNT_lastBus,
            // NT triggers route through CV inputs in the bus space, so a CV
            // input bus is the natural default; pick buses 5..8 so they
            // don't shadow the four CV-in defaults.
            static_cast<int16_t>(5 + i),
            kNT_unitCvInput, 0, nullptr
        };
    }
}

// Build one settings parameter row from the vendor value_attr. The
// enum-label offset rule lives here: pass value_names + min_ as the NT
// string array because vendor menus index value_names[value] absolutely
// while NT enum parameters index min-relative (strings[value - min]).
inline _NT_parameter param_from_value_attr(const settings::value_attr& va) {
    _NT_parameter p{};
    p.name = va.name;
    p.min  = static_cast<int16_t>(va.min_);
    p.max  = static_cast<int16_t>(va.max_);
    p.def  = static_cast<int16_t>(va.default_);
    if (va.value_names != nullptr) {
        p.unit         = kNT_unitEnum;
        p.enumStrings  = va.value_names + va.min_;
    } else {
        p.unit         = kNT_unitNone;
        p.enumStrings  = nullptr;
    }
    p.scaling = 0;
    return p;
}

// ---------------------------------------------------------------------------
// Construct
// ---------------------------------------------------------------------------

// Construct without a settings facade (test scaffold: enables the cadence,
// edge, and centering tests that don't touch the settings path).
inline void construct(AppAlgorithm& alg, const OC::App* app) {
    alg.app = app;
    alg.settings_facade = SettingsFacade{};

    emit_io_params(alg.parameters_storage);
    // Initialize v[] from the I/O defaults.
    for (int i = 0; i < kIoParamCount; ++i) {
        alg.v_storage[i] = alg.parameters_storage[i].def;
    }
    alg.parameters     = alg.parameters_storage;
    alg.parameterPages = nullptr;
    const_cast<int16_t*&>(alg.v) = alg.v_storage;

    alg.tick_numerator = 0;
    alg.idle_count     = 0;
    alg.alive          = false;
    OC::CORE::ticks    = 0;
    alg.last_controls  = 0;
    for (size_t i = 0; i < sizeof(alg.held_since) / sizeof(alg.held_since[0]); ++i) {
        alg.held_since[i] = 0;
    }

    OC::apps::current_app = app;

    if (app && app->Init) app->Init();
    if (app && app->HandleAppEvent) app->HandleAppEvent(OC::APP_EVENT_RESUME);
}

// Construct with a typed settings instance. The facade captures the typed
// call sites; the parameter table extends with one row per setting.
template <typename Settings>
inline void construct(AppAlgorithm& alg, const OC::App* app,
                      Settings* settings, int num_settings) {
    construct(alg, app);
    alg.settings_facade = make_facade(settings);
    alg.settings_facade.num_settings = num_settings;

    // Append settings parameters after the I/O block. value_attr_ is a static
    // member of SettingsBase<clazz, N>; read it through the facade so this
    // function's body stays decoupled from the concrete Settings type for
    // anything below the make_facade<> call.
    for (int i = 0; i < num_settings; ++i) {
        const auto* va = alg.settings_facade.value_attr_at(i);
        alg.parameters_storage[kIoParamCount + i] = param_from_value_attr(*va);
        // Seed v[] from the current Settings value (after InitDefaults).
        alg.v_storage[kIoParamCount + i] =
            static_cast<int16_t>(alg.settings_facade.get_value(
                alg.settings_facade.instance, i));
    }
}

// ---------------------------------------------------------------------------
// I/O routing param accessors and bus <-> OC backing conversion
// ---------------------------------------------------------------------------
//
// Routing param layout in v[] (see emit_io_params):
//   [0 .. kNumCvInputs)                          CV-in bus selections
//   [kNumCvInputs .. kNumCvInputs+kNumCvOutputs) CV-out bus selections
//   [.. + kNumTrigInputs)                        TR-in bus selections
// Each value is a 1-indexed NT bus (0 == not routed), matching the
// hem_shim bus convention (v[bus_param]; src = busFrames + (bus-1)*numFrames).
//
// Read the firmware/host-managed published array (alg.v), not the private
// v_storage snapshot: alg.v is repointed away from v_storage after construct
// (NT API: v is "Managed by the system"), so a routing param edit lands in
// alg.v and only reading alg.v makes the re-route take effect live. These
// accessors run from step()/draw() only, after construct() has set alg.v, so
// alg.v is always valid here.
inline int cv_in_bus(const AppAlgorithm& alg, int i)  { return alg.v[i]; }
inline int cv_out_bus(const AppAlgorithm& alg, int i) { return alg.v[kNumCvInputs + i]; }
inline int trig_in_bus(const AppAlgorithm& alg, int i) {
    return alg.v[kNumCvInputs + kNumCvOutputs + i];
}

// Bus <-> hem-unit conversions. The NT input/output bus carries 1V/oct in
// float volts; the OC input store and the unbiased pitch space use hem units
// at 1536 per octave (12 << 7), matching hem_shim::copy_bus_to_frame and
// OC_DAC.h kIntervalSize. OC::DAC::value() is biased by kOctaveZero octaves
// (0V == kOctaveZero * kIntervalSize), so the flush subtracts that bias.
constexpr float kHemUnitsPerVolt = 1536.0f;
constexpr int   kDacZeroBias = static_cast<int>(OC::DAC::kOctaveZero) * OC::DAC::kIntervalSize;

// Read the mean level of a routed bus across the buffer and store it in the
// OC input backing as hem units. bus 0 (not routed) clears the channel.
inline void route_cv_input(int channel, int bus, const float* busFrames, int numFrames) {
    if (bus <= 0 || busFrames == nullptr) { oc_io::set_input(channel, 0); return; }
    const float* src = busFrames + (bus - 1) * numFrames;
    float sum = 0.0f;
    for (int i = 0; i < numFrames; ++i) sum += src[i];
    const float mean = sum / static_cast<float>(numFrames);
    oc_io::set_input(channel, static_cast<int>(mean * kHemUnitsPerVolt));
}

// Latch the routed trigger bus level (high if any sample crosses the gate
// threshold) into the OC trigger backing. The Scan/edge discipline in step()
// converts a held-high level into exactly one rising edge per low->high.
inline void route_trigger_input(int input, int bus, const float* busFrames, int numFrames) {
    if (bus <= 0 || busFrames == nullptr) { oc_io::set_trigger(input, false); return; }
    const float* src = busFrames + (bus - 1) * numFrames;
    bool high = false;
    for (int i = 0; i < numFrames; ++i) {
        if (src[i] > 0.5f) { high = true; break; }
    }
    oc_io::set_trigger(input, high);
}

// Write one OC DAC channel onto a routed CV-out bus in replace mode, after
// unbiasing the DAC value and converting from hem units to NT volts.
inline void route_cv_output(int channel, int bus, float* busFrames, int numFrames) {
    if (bus <= 0 || busFrames == nullptr) return;
    float* dst = busFrames + (bus - 1) * numFrames;
    const int dac = static_cast<int>(OC::DAC::value(static_cast<size_t>(channel)));
    const float volts = static_cast<float>(dac - kDacZeroBias) / kHemUnitsPerVolt;
    for (int i = 0; i < numFrames; ++i) dst[i] = volts;
}

inline void refresh_oc_inputs(AppAlgorithm& alg, const float* busFrames, int numFrames) {
    for (int i = 0; i < kNumCvInputs; ++i) {
        route_cv_input(i, cv_in_bus(alg, i), busFrames, numFrames);
    }
    for (int i = 0; i < kNumTrigInputs; ++i) {
        route_trigger_input(i, trig_in_bus(alg, i), busFrames, numFrames);
    }
}

inline void flush_oc_outputs(AppAlgorithm& alg, float* busFrames, int numFrames) {
    for (int i = 0; i < kNumCvOutputs; ++i) {
        route_cv_output(i, cv_out_bus(alg, i), busFrames, numFrames);
    }
}

// ---------------------------------------------------------------------------
// Step: cadence accumulator + one-edge-per-tick + isr dispatch
// ---------------------------------------------------------------------------
//
// The runtime needs the NT sample rate. Read it from NT_globals at first call
// so tests using the host's stub NT_globals (48 kHz) get the right cadence.
#ifdef NT_HEM_HOST_SIM
// Host-test seam: NT_globals is const on host, so tests cannot mutate the
// sample rate to exercise the sr == 0 guard in step(). A negative value means
// "use NT_globals.sampleRate". Compiled out of the ARM plug-in entirely
// (NT_HEM_HOST_SIM is set only in HOST_FLAGS), so production carries no seam.
inline int32_t& sample_rate_override() {
    static int32_t v = -1;
    return v;
}
#endif

inline uint32_t sample_rate_hz() {
#ifdef NT_HEM_HOST_SIM
    if (sample_rate_override() >= 0) {
        return static_cast<uint32_t>(sample_rate_override());
    }
#endif
    return NT_globals.sampleRate;
}

// step(): the bus-routing overload. Refreshes the OC input backing from the
// routed CV-in / TR-in buses at the start of the buffer, runs the cadence
// accumulator's isr ticks, then flushes the OC DAC onto the routed CV-out
// buses. busFrames may be null (the cadence/edge-only test overload below
// passes null): with no bus the routing helpers fall through to the
// test-injected oc_io backing.
inline void step(AppAlgorithm& alg, float* busFrames, int num_frames) {
    if (!alg.app || !alg.app->isr) return;

    // Sample the routed buses once at the start of the buffer (frame zero).
    // Per-sample CV is a refinement: the OC isr cadence (~16.666 kHz) runs
    // many ticks per audio buffer, but the routed input is held constant
    // across those ticks here. The one-edge-per-tick guarantee for triggers
    // comes from the Scan discipline below, not from per-sample sampling.
    if (busFrames != nullptr) {
        refresh_oc_inputs(alg, busFrames, num_frames);
    }

    // Add this buffer's contribution to the cadence accumulator.
    const uint64_t sr = sample_rate_hz();
    // Guard against an unestablished sample rate. The firmware runs step()
    // during add-algorithm before the audio rate is set for the new instance,
    // so NT_globals.sampleRate can be 0. An unguarded `while (numerator >= 0)`
    // would spin forever (watchdog fault, surfaced on-device as "Failed to add
    // algorithm"). Skip the step until the rate is valid; leave the accumulator
    // untouched so the cadence stays exact once audio starts.
    if (sr == 0) return;
    alg.tick_numerator += static_cast<uint64_t>(num_frames) *
                          static_cast<uint64_t>(OC_CORE_ISR_FREQ);

    while (alg.tick_numerator >= sr) {
        alg.tick_numerator -= sr;

        // Scan once per isr tick. The first tick that sees a low->high
        // transition latches the rising edge; later ticks in the same buffer
        // re-run Scan with last_high_ == trigger_high_, so a still-high bus
        // produces no further edge. This is the one-edge-per-tick discipline.
        OC::DigitalInputs::Scan();

        OC::CORE::ticks += 1;
        alg.app->isr();
    }

    // Flush the OC DAC onto the routed CV-out buses after the isr ticks have
    // had a chance to update it.
    if (busFrames != nullptr) {
        flush_oc_outputs(alg, busFrames, num_frames);
    }
}

// Cadence/edge-only overload: drives the runtime with a frame count and no
// bus. Routing helpers are skipped so tests can inject the OC backing via
// oc_io::set_input / set_trigger directly.
inline void step(AppAlgorithm& alg, int num_frames) {
    step(alg, nullptr, num_frames);
}

// Factory thunk passes numFramesBy4 (api.h:447); the runtime converts.
inline void step_factory(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* alg = static_cast<AppAlgorithm*>(self);
    step(*alg, busFrames, numFramesBy4 * 4);
}

// ---------------------------------------------------------------------------
// Draw: idle counter + loop() + DrawMenu/Screensaver + centering shift
// ---------------------------------------------------------------------------

// Idle ticks before the screensaver kicks in. Vendor uses a multi-second
// threshold; for the foundation 60 draws is enough to cover both idle and
// active paths under host tests.
constexpr uint32_t kScreensaverIdleThreshold = 60;

inline void center_screen_into_192() {
    // Each of the 64 rows on NT_screen is 128 bytes wide (256 pixels packed
    // 2-per-byte). The app drew into vendor x [0, 128), which maps to
    // byte offsets [0, 64) within the row. Move those 64 bytes to offsets
    // [32, 96) and zero the [0, 32) left margin and [96, 128) right margin.
    for (int row = 0; row < 64; ++row) {
        uint8_t* base = NT_screen + row * 128;
        std::memmove(base + 32, base, 64);
        std::memset(base, 0, 32);
        std::memset(base + 96, 0, 32);
    }
}

inline void draw(AppAlgorithm& alg) {
    if (!alg.app) return;

    // Clear the canvas so the centering shift only carries this draw's
    // emissions, not whatever the prior step left behind.
    std::memset(NT_screen, 0, 128 * 64);

    if (alg.app->loop) alg.app->loop();

    const bool screensaving = alg.idle_count >= kScreensaverIdleThreshold;
    if (screensaving && alg.app->DrawScreensaver) {
        alg.app->DrawScreensaver();
    } else if (alg.app->DrawMenu) {
        alg.app->DrawMenu();
    }

    center_screen_into_192();

    alg.idle_count += 1;
    alg.alive = true;
}

inline bool draw_factory(_NT_algorithm* self) {
    auto* alg = static_cast<AppAlgorithm*>(self);
    draw(*alg);
    return true;  // suppress the firmware parameter line
}

// ---------------------------------------------------------------------------
// parameterChanged: sentinel-guarded
// ---------------------------------------------------------------------------

inline void parameterChanged(AppAlgorithm& alg, int idx) {
    if (!alg.alive) return;  // construct-time spurious fire: suppress
    if (idx < 0) return;

    if (idx < kIoParamCount) {
        // I/O routing change. The runtime samples the routed bus at the
        // next step() call; no immediate action required.
        return;
    }
    const int s = idx - kIoParamCount;
    if (s >= alg.settings_facade.num_settings) return;
    if (alg.settings_facade.apply_value == nullptr) return;
    // Read the firmware/host-managed published parameter array (alg.v), not the
    // private v_storage snapshot. Per the NT API, v is "Managed by the system":
    // construct() seeds v == v_storage, but the firmware then repoints alg.v to
    // its own parameter store (the harness loader does exactly the same), so
    // after construct the two arrays diverge and only alg.v carries live edits.
    // The firmware (and NT_setParameterFromUi) write through alg.v, so
    // parameterChanged and all live parameter reads must use alg.v.
    alg.settings_facade.apply_value(alg.settings_facade.instance, s,
                                    alg.v[idx]);
}

inline void parameterChanged_factory(_NT_algorithm* self, int idx) {
    parameterChanged(*static_cast<AppAlgorithm*>(self), idx);
}

// ---------------------------------------------------------------------------
// Serialise / Deserialise: SettingsBase Save/Restore blob, base64-wrapped
// ---------------------------------------------------------------------------

namespace b64 {
inline char encode_char(unsigned v) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    return tbl[v & 0x3F];
}
inline int decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Encode `len` input bytes into NUL-terminated b64. Returns chars written
// (excluding the NUL).
inline int encode(const uint8_t* in, int len, char* out) {
    int o = 0;
    int i = 0;
    while (i + 3 <= len) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        out[o++] = encode_char(v >> 18);
        out[o++] = encode_char(v >> 12);
        out[o++] = encode_char(v >> 6);
        out[o++] = encode_char(v);
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = encode_char(v >> 18);
        out[o++] = encode_char(v >> 12);
        out[o++] = '=';
        out[o++] = '=';
    } else if (i + 2 == len) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8;
        out[o++] = encode_char(v >> 18);
        out[o++] = encode_char(v >> 12);
        out[o++] = encode_char(v >> 6);
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

// Decode `len` base64 chars into out, writing at most max_out bytes. Returns
// the number of bytes written, or -1 on malformed input.
inline int decode(const char* in, int len, uint8_t* out, int max_out) {
    int o = 0;
    int buf = 0;
    int bits = 0;
    for (int i = 0; i < len; ++i) {
        char c = in[i];
        if (c == '=' || c == '\0') break;
        int v = decode_char(c);
        if (v < 0) return -1;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= max_out) return -1;
            out[o++] = (uint8_t)((buf >> bits) & 0xFF);
        }
    }
    return o;
}
}  // namespace b64

// Build "oc_w<idx>" (settings-blob word member name) into buf (>= 12 bytes).
inline void blob_word_name(int idx, char* buf) {
    buf[0] = 'o'; buf[1] = 'c'; buf[2] = '_'; buf[3] = 'w';
    char tmp[8];
    int t = 0;
    if (idx == 0) {
        tmp[t++] = '0';
    } else {
        for (int x = idx; x > 0; x /= 10) tmp[t++] = static_cast<char>('0' + x % 10);
    }
    int o = 4;
    while (t > 0) buf[o++] = tmp[--t];
    buf[o] = '\0';
}

// Settings persistence uses addNumber / parse.number ONLY. The disting NT
// firmware faults when a plug-in calls _NT_jsonStream::addString during the
// add-algorithm path (confirmed on hardware by bisection: the working
// per-applet runtime serialises with addNumber and adds cleanly, while an
// addString-based serialiser makes add-algorithm fail). The SettingsBase
// Save() blob is therefore packed four bytes per JSON number, under the members
// "oc_len" (byte count) and "oc_w0".."oc_wN" (the packed little-endian words).
inline void serialise(AppAlgorithm& alg, _NT_jsonStream& stream) {
    if (alg.settings_facade.save == nullptr) return;
    // Guard the fixed-size stack blob: an app whose storage footprint exceeds
    // kMaxBlobBytes would overrun on Save. Bail rather than corrupt the stack.
    if (alg.settings_facade.storage_size != nullptr &&
        alg.settings_facade.storage_size() > static_cast<size_t>(kMaxBlobBytes)) {
        return;
    }
    uint8_t blob[kMaxBlobBytes] = {0};
    const int n = static_cast<int>(
        alg.settings_facade.save(alg.settings_facade.instance, blob));
    stream.addMemberName("oc_len");
    stream.addNumber(n);
    const int words = (n + 3) / 4;
    for (int w = 0; w < words; ++w) {
        uint32_t packed = 0;
        for (int b = 0; b < 4; ++b) {
            const int idx = w * 4 + b;
            if (idx < n) packed |= static_cast<uint32_t>(blob[idx]) << (b * 8);
        }
        char name[12];
        blob_word_name(w, name);
        stream.addMemberName(name);
        stream.addNumber(static_cast<int>(packed));
    }
}

inline bool deserialise(AppAlgorithm& alg, _NT_jsonParse& parse) {
    if (alg.settings_facade.restore == nullptr) return false;
    if (alg.settings_facade.storage_size != nullptr &&
        alg.settings_facade.storage_size() > static_cast<size_t>(kMaxBlobBytes)) {
        return false;
    }

    int num_members = 0;
    if (!parse.numberOfObjectMembers(num_members)) return false;

    uint8_t blob[kMaxBlobBytes] = {0};
    int blob_len = -1;

    for (int i = 0; i < num_members; ++i) {
        if (parse.matchName("oc_len")) {
            if (!parse.number(blob_len)) return false;
            continue;
        }
        bool matched = false;
        for (int w = 0; w < kMaxBlobBytes / 4; ++w) {
            char name[12];
            blob_word_name(w, name);
            if (parse.matchName(name)) {
                int packed = 0;
                if (!parse.number(packed)) return false;
                for (int b = 0; b < 4; ++b) {
                    const int idx = w * 4 + b;
                    if (idx < kMaxBlobBytes) {
                        blob[idx] = static_cast<uint8_t>(
                            (static_cast<uint32_t>(packed) >> (b * 8)) & 0xFF);
                    }
                }
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (!parse.skipMember()) return false;
        }
    }
    if (blob_len < 0 || blob_len > kMaxBlobBytes) return false;

    alg.settings_facade.restore(alg.settings_facade.instance, blob);
    return true;
}

inline void serialise_factory(_NT_algorithm* self, _NT_jsonStream& stream) {
    serialise(*static_cast<AppAlgorithm*>(self), stream);
}

inline bool deserialise_factory(_NT_algorithm* self, _NT_jsonParse& parse) {
    return deserialise(*static_cast<AppAlgorithm*>(self), parse);
}

// ---------------------------------------------------------------------------
// customUi: control router + composition primitives
// ---------------------------------------------------------------------------
//
// The runtime owns the control-edge bookkeeping (held_since timestamps,
// last_controls mask, idle reset). Per-app .cpp TUs own the actual UI::Event
// construction, because OC::UI::Event is only forward-declared in OC_apps.h;
// the runtime header deliberately does not pull vendor UI/ui_events.h so it
// stays decoupled from the concrete event type. The runtime exposes the
// classification (short vs long), the long-press threshold, and accessors for
// the tracked state so a per-app TU can compose a UI::Event cleanly.
//
// Expected composition in a per-app .cpp customUi entry point:
//
//   void customUi(_NT_algorithm* self, const _NT_uiData& data) {
//       auto* alg = static_cast<oc_runtime::AppAlgorithm*>(self);
//       const uint16_t edges = data.controls ^ data.lastButtons;
//       for each mapped control bit `bit` with index `bi`:
//           const bool now_down = data.controls & bit;
//           if (edges & bit) {
//               if (now_down) {
//                   // press DOWN: runtime records held_since on its own pass
//               } else {
//                   // RELEASE: classify and emit
//                   uint8_t ev = oc_runtime::classify_release(alg, bi);
//                   ::UI::Event e(static_cast<::UI::EventType>(ev),
//                                 mapped_oc_control, 0, /*mask=*/0);
//                   alg->app->HandleButtonEvent(
//                       reinterpret_cast<const OC::UI::Event&>(e));
//               }
//           } else if (now_down &&
//                      oc_runtime::was_long_press_already_emitted(alg, bi) &&
//                      !already_emitted_long_press_for(bi)) {
//               // emit EVENT_BUTTON_LONG_PRESS once, then mark it emitted
//           }
//       // forward to oc_runtime::customUi(*alg, data) for held_since/idle
//       oc_runtime::customUi(*alg, data);
//   }
//
// The runtime's own customUi() updates held_since / last_controls / idle on
// each call; the per-app TU calls it after emitting events so the bookkeeping
// reflects the post-event state.

constexpr uint64_t kLongPressTicks = (OC_CORE_ISR_FREQ * 500ULL) / 1000ULL;

// Event-type values mirroring vendor UI::EventType (UI/ui_events.h). The
// runtime returns these as plain uint8_t so it need not include vendor UI;
// the per-app TU casts to ::UI::EventType. Keep in sync with the vendor enum:
//   EVENT_NONE=0, EVENT_BUTTON_DOWN=1, EVENT_BUTTON_PRESS=2,
//   EVENT_BUTTON_LONG_PRESS=3, EVENT_BUTTON_LONG_RELEASE=4, EVENT_ENCODER=5.
constexpr uint8_t EVENT_NONE              = 0;
constexpr uint8_t EVENT_BUTTON_DOWN       = 1;
constexpr uint8_t EVENT_BUTTON_PRESS      = 2;
constexpr uint8_t EVENT_BUTTON_LONG_PRESS = 3;
constexpr uint8_t EVENT_BUTTON_LONG_RELEASE = 4;
constexpr uint8_t EVENT_ENCODER           = 5;

// Accessors for the runtime's tracked customUi state (F7). A per-app TU reads
// these to build the UI::Event mask and to detect short vs long presses.
inline uint64_t held_since_at(const AppAlgorithm& alg, int bit_index) {
    return alg.held_since[bit_index];
}
inline uint16_t last_controls_of(const AppAlgorithm& alg) {
    return alg.last_controls;
}

// True if the control at bit_index has been held past the long-press
// threshold (so a LONG_PRESS event would have been emitted by now). Lets a
// per-app TU avoid double-emitting between the long-press fire and release.
inline bool was_long_press_already_emitted(AppAlgorithm* alg, int bit_index) {
    const uint64_t held = OC::CORE::ticks - alg->held_since[bit_index];
    return held >= kLongPressTicks;
}

// Classify a release on the control at bit_index: a release after a long hold
// returns EVENT_BUTTON_LONG_RELEASE, otherwise EVENT_BUTTON_PRESS (vendor
// semantics: a short press fires PRESS on release, see UI::Event::IsRelease).
inline uint8_t classify_release(AppAlgorithm* alg, int bit_index) {
    return was_long_press_already_emitted(alg, bit_index)
               ? EVENT_BUTTON_LONG_RELEASE
               : EVENT_BUTTON_PRESS;
}

// Mapping from kNT_* control bits to OC::UiControl values. Buttons 3 and 4
// are Up/Down; encoder pushes are L/R; encoders themselves emit ENCODER_L/R.
struct ControlMapping {
    uint16_t nt_bit;
    uint16_t oc_control;
};
inline const ControlMapping* button_mapping_table(int& count) {
    static const ControlMapping table[] = {
        { kNT_button3,        OC::CONTROL_BUTTON_UP },
        { kNT_button4,        OC::CONTROL_BUTTON_DOWN },
        { kNT_encoderButtonL, OC::CONTROL_BUTTON_L },
        { kNT_encoderButtonR, OC::CONTROL_BUTTON_R },
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

inline int bit_index(uint16_t bit) {
    int i = 0;
    while (bit > 1) { bit >>= 1; ++i; }
    return i;
}

inline uint32_t hasCustomUi_factory(_NT_algorithm* /*self*/) {
    return kNT_button3 | kNT_button4 |
           kNT_encoderButtonL | kNT_encoderButtonR |
           kNT_encoderL | kNT_encoderR;
}

// Runtime-side customUi bookkeeping. Records the down tick for each mapped
// control so classify_release / was_long_press_already_emitted can read it,
// snapshots the controls mask, and resets the idle counter on any activity.
// The per-app .cpp owns the actual UI::Event dispatch (see the composition
// pseudocode above); this function only tracks state.
inline void customUi(AppAlgorithm& alg, const _NT_uiData& data) {
    if (!alg.app) return;

    int n = 0;
    const ControlMapping* tbl = button_mapping_table(n);

    const uint16_t edges = data.controls ^ data.lastButtons;

    for (int i = 0; i < n; ++i) {
        const uint16_t bit  = tbl[i].nt_bit;
        const bool now_down = (data.controls & bit) != 0;
        const int  bi       = bit_index(bit);
        const bool transition = (edges & bit) != 0;

        if (transition && now_down) {
            alg.held_since[bi] = OC::CORE::ticks;
        }
    }

    alg.last_controls = data.controls;

    // Any control activity resets the idle counter.
    if (data.controls != 0 || data.encoders[0] != 0 || data.encoders[1] != 0) {
        alg.idle_count = 0;
    }
}

inline void customUi_factory(_NT_algorithm* self, const _NT_uiData& data) {
    customUi(*static_cast<AppAlgorithm*>(self), data);
}

}  // namespace oc_runtime
