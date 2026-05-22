// Host test: Hemispheres_host parameter proxying.
//
// Tests for the proxy aggregator wiring on plugins/hosts/Hemispheres_host.cpp.
// Covers selector enum population, selector-edit reaggregation, and the
// construct-time guard for proxy forwarding (draw_count == 0 suppresses the
// forward; draw_count > 0 routes via NT_setParameterFromUi).
//
// Separate file from test_host_Hemispheres_host.cpp so the new build rule
// can link shim/src/host_proxy.cpp without disturbing the pre-existing
// test_host_% pattern rule (Quadrants_host stage 3b owns that rule).
//
// Test seams required from the host:
//   - hp_test_inject_slot / hp_test_clear_slots  (host_proxy injection)
//   - hh_test_get_state                          (host_proxy::State* accessor)
//   - hh_test_inject_slot already in place for slot-resolution stubs.

#include <cstddef>

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>
#include "HemiPluginInterface.h"
#include "host_proxy.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
void hh_test_inject_slot(int slot_idx, HemiPluginInterface* plugin, uint32_t guid);
void hh_test_clear_slots(void);
host_proxy::State* hh_test_get_state(_NT_algorithm* alg);
}

namespace {

constexpr uint32_t kHemiGuid_Cu = static_cast<uint32_t>('H') |
                                  (static_cast<uint32_t>('m') << 8) |
                                  (static_cast<uint32_t>('C') << 16) |
                                  (static_cast<uint32_t>('u') << 24);
constexpr uint32_t kHemiGuid_St = static_cast<uint32_t>('H') |
                                  (static_cast<uint32_t>('m') << 8) |
                                  (static_cast<uint32_t>('S') << 16) |
                                  (static_cast<uint32_t>('1') << 24);

const _NT_parameter sample_params_3[] = {
    { "Trigger",   0, 1,    0,   kNT_unitNone, 0, nullptr },
    { "Length",    1, 64,   8,   kNT_unitNone, 0, nullptr },
    { "Threshold", 0, 1023, 512, kNT_unitNone, 0, nullptr },
};

struct ProxySetup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    host_proxy::State* state;
};

ProxySetup make_proxy_setup() {
    nt::reset_runtime();
    hh_test_clear_slots();
    host_proxy::hp_test_clear_slots();
    // Inject two Hemi-prefix slots BEFORE loading the host so its construct
    // path can resolve initial enum entries.
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::hp_test_inject_slot(1, "Stairs",  kHemiGuid_St, 3, sample_params_3);

    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    host_proxy::State* state = hh_test_get_state(loaded->algorithm);
    REQUIRE(state != nullptr);
    return ProxySetup{ loaded, loaded->algorithm, state };
}

}  // namespace

// ---------------------------------------------------------------------------
// HP1: calculateRequirements reports the full proxy table budget.
// ---------------------------------------------------------------------------

TEST_CASE("HP1: calculateRequirements reports proxy-aware numParameters",
          "[host][hemispheres_host][proxy]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    _NT_algorithmRequirements req{};
    loaded->factory->calculateRequirements(req, nullptr);
    // Hemispheres uses K = 2 lanes: 2 selectors + 2 * 16 proxy params = 34.
    constexpr int kHostSlots = 2;
    int expected = kHostSlots + kHostSlots * host_proxy::kMaxProxyParamsPerSlot;
    REQUIRE(req.numParameters == static_cast<uint32_t>(expected));
}

// ---------------------------------------------------------------------------
// HP2: construct installs two selector params bound to the enum table.
// ---------------------------------------------------------------------------

TEST_CASE("HP2: selectors at indices 0 and 1 are enum-bound to State::enum_strs",
          "[host][hemispheres_host][proxy]") {
    auto s = make_proxy_setup();
    REQUIRE(s.alg->parameters != nullptr);
    REQUIRE(s.alg->parameters[0].unit == kNT_unitEnum);
    REQUIRE(s.alg->parameters[1].unit == kNT_unitEnum);
    REQUIRE(s.alg->parameters[0].enumStrings == s.state->enum_strs.table);
    REQUIRE(s.alg->parameters[1].enumStrings == s.state->enum_strs.table);
    REQUIRE(std::string(s.alg->parameters[0].name) == "Slot 0");
    REQUIRE(std::string(s.alg->parameters[1].name) == "Slot 1");
}

// ---------------------------------------------------------------------------
// HP3: construct refreshes enum strings from the injected preset.
// ---------------------------------------------------------------------------

TEST_CASE("HP3: enum_strs holds \"---\" + injected Hemi slots after construct",
          "[host][hemispheres_host][proxy]") {
    auto s = make_proxy_setup();
    REQUIRE(s.state->enum_strs.count == 3);  // --- plus Cumulus plus Stairs
    REQUIRE(std::string(s.state->enum_strs.table[0]) == "---");
    REQUIRE(std::strstr(s.state->enum_strs.table[1], "Cumulus") != nullptr);
    REQUIRE(std::strstr(s.state->enum_strs.table[2], "Stairs")  != nullptr);
}

// ---------------------------------------------------------------------------
// HP4: construct aggregates each lane based on default selector value.
// ---------------------------------------------------------------------------

TEST_CASE("HP4: construct aggregates lane 0 and lane 1 from default selectors",
          "[host][hemispheres_host][proxy]") {
    auto s = make_proxy_setup();
    // Defaults: lane 0 -> enum value 1 (Cumulus at preset slot 0).
    // Lane 1 -> enum value 2 (Stairs at preset slot 1).
    REQUIRE(s.state->maps[0].slot_idx == 0u);
    REQUIRE(s.state->maps[0].slot_param_cnt == 3);
    REQUIRE(s.state->maps[1].slot_idx == 1u);
    REQUIRE(s.state->maps[1].slot_param_cnt == 3);

    int base0 = s.state->kNumSlotIndexParams;
    int base1 = s.state->kNumSlotIndexParams + host_proxy::kMaxProxyParamsPerSlot;
    REQUIRE(std::strncmp(s.alg->parameters[base0 + 0].name, "S0 ", 3) == 0);
    REQUIRE(std::strstr(s.alg->parameters[base0 + 0].name, "Trigger") != nullptr);
    REQUIRE(std::strncmp(s.alg->parameters[base1 + 0].name, "S1 ", 3) == 0);
    REQUIRE(std::strstr(s.alg->parameters[base1 + 0].name, "Trigger") != nullptr);
}

// ---------------------------------------------------------------------------
// HP5: editing a selector triggers reaggregation onto the new slot.
// ---------------------------------------------------------------------------

TEST_CASE("HP5: parameterChanged on a selector reaggregates that lane",
          "[host][hemispheres_host][proxy]") {
    auto s = make_proxy_setup();
    // Lane 0 starts pointing at slot 0 (Cumulus). Move it to slot 1 (Stairs)
    // by writing enum value 2 into v[0] and firing parameterChanged(0).
    int16_t* writable_v = const_cast<int16_t*>(s.alg->v);
    writable_v[0] = 2;
    s.loaded->factory->parameterChanged(s.alg, 0);

    REQUIRE(s.state->maps[0].slot_idx == 1u);
    REQUIRE(s.state->maps[0].slot_param_cnt == 3);
}

// ---------------------------------------------------------------------------
// HP6: editing a selector to "---" clears the lane.
// ---------------------------------------------------------------------------

TEST_CASE("HP6: parameterChanged on a selector with enum 0 clears the lane",
          "[host][hemispheres_host][proxy]") {
    auto s = make_proxy_setup();
    int16_t* writable_v = const_cast<int16_t*>(s.alg->v);
    writable_v[0] = 0;
    s.loaded->factory->parameterChanged(s.alg, 0);

    REQUIRE(s.state->maps[0].slot_idx == host_proxy::kInvalidSlotIdx);
    REQUIRE(s.state->maps[0].slot_param_cnt == 0);
}

// ---------------------------------------------------------------------------
// HP7: proxy parameterChanged before any draw is suppressed (construct-time
//      guard). The host's state.draw_count must be 0 here and the
//      NT_setParameterFromUi forward must not be logged.
// ---------------------------------------------------------------------------

TEST_CASE("HP7: parameterChanged on a proxy is suppressed when draw_count == 0",
          "[host][hemispheres_host][proxy]") {
    auto s = make_proxy_setup();
    REQUIRE(s.state->draw_count == 0u);

    // Open the param log so any NT_setParameterFromUi call records "idx value\n".
    FILE* log = std::tmpfile();
    REQUIRE(log != nullptr);
    nt::set_param_log(log);

    // Edit proxy param at index K (lane 0, slot param 0).
    int proxy_idx = s.state->kNumSlotIndexParams;
    int16_t* writable_v = const_cast<int16_t*>(s.alg->v);
    writable_v[proxy_idx] = 7;
    s.loaded->factory->parameterChanged(s.alg, proxy_idx);

    nt::set_param_log(nullptr);
    std::fflush(log);
    std::fseek(log, 0, SEEK_END);
    long n = std::ftell(log);
    std::fclose(log);
    REQUIRE(n == 0);  // no forward recorded
}

// ---------------------------------------------------------------------------
// HP8: proxy parameterChanged after at least one draw forwards via
//      NT_setParameterFromUi to the watched slot's parameter.
//
//      The harness's NT_setParameterFromUi only writes when algorithmIndex
//      resolves to the single registered plugin (the host itself, algIdx 0).
//      We aggregate lane 0 from injected slot 0 in make_proxy_setup, but
//      slot_idx 0 is also the host's own NT_algorithmIndex. So the forward
//      will write "0 <value>\n" to the param log -- this is the observable
//      effect we assert.
// ---------------------------------------------------------------------------

TEST_CASE("HP8: parameterChanged on a proxy forwards via NT_setParameterFromUi after draw",
          "[host][hemispheres_host][proxy]") {
    auto s = make_proxy_setup();

    // Drive a draw to flip draw_count > 0.
    s.loaded->factory->draw(s.alg);
    REQUIRE(s.state->draw_count > 0u);

    FILE* log = std::tmpfile();
    REQUIRE(log != nullptr);
    nt::set_param_log(log);

    // Edit proxy param at K + 1 (lane 0, slot_param_idx 1 -> "Length").
    int proxy_idx = s.state->kNumSlotIndexParams + 1;
    int16_t* writable_v = const_cast<int16_t*>(s.alg->v);
    writable_v[proxy_idx] = 33;
    s.loaded->factory->parameterChanged(s.alg, proxy_idx);

    nt::set_param_log(nullptr);
    std::fflush(log);
    std::rewind(log);
    char buf[64] = { 0 };
    size_t got = std::fread(buf, 1, sizeof(buf) - 1, log);
    std::fclose(log);
    REQUIRE(got > 0);
    // First forward line must be "<slot_param_idx> <value>\n" for slot_idx 0.
    // slot_param_idx is 1 in the watched slot's table.
    std::string line(buf);
    REQUIRE(line.find("1 33") != std::string::npos);
}
