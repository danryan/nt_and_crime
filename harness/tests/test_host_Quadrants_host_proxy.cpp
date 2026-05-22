// Host test: Quadrants Host parameter proxying via host_proxy.
//
// Verifies the per-applet host UX rework wiring (spec
// docs/superpowers/specs/2026-05-21-host-ux-rework-design.md) for the
// Quadrants host:
//
//   - construct populates 4 selectors at v[0..3] each bound to host_proxy
//     state's enum table.
//   - parameterChanged(self, lane) for a selector index refreshes enum
//     strings and reaggregates that lane's proxy params.
//   - parameterChanged(self, host_p) for a proxy index forwards through
//     NT_setParameterFromUi when draw_count > 0, and is suppressed during
//     the construct-time fan-out when draw_count == 0.
//   - Focused-slot UX (button1-4) and serialise/deserialise round-trip of
//     focused_slot_idx remain functional after the proxy aggregator is in
//     place.
//
// host_proxy injects watched-slot stubs via the same NT_HEM_HOST_SIM seam
// used by harness/tests/test_host_proxy.cpp. NT_setParameterFromUi in the
// harness writes the value into the host's own v[] only when the host's
// algorithm index (0) is the target; the proxy forward path is exercised
// by binding a lane to slot index 0 (the host itself) and observing the
// resulting v[] write plus the recursive parameterChanged callback. Slots
// bound to non-zero indices receive no observable write (harness only
// knows about its single registered algorithm) which is a useful negative
// signal: the call returns without crashing.

#include <cstddef>

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "nt_jsonstream.h"

#include <distingnt/api.h>
#include "HemiPluginInterface.h"
#include "host_proxy.h"

#include <cstdint>
#include <cstring>
#include <string>

// Declarations for the test-injection API exported by Quadrants_host.cpp
// under NT_HEM_HOST_SIM. Used here to drive the focused-slot side-channel
// in the few cases that verify the existing UX still works alongside the
// new proxy state.
extern "C" {
void    qq_test_inject_slot(int slot_idx, HemiPluginInterface* plugin, uint32_t guid);
void    qq_test_clear_slots(void);
uint8_t qq_test_get_focused_slot(const _NT_algorithm* self);
void    qq_test_set_focused_slot(_NT_algorithm* self, uint8_t idx);
}

namespace {

// Hemi-prefix guids for the host_proxy injection table. The low 16 bits
// of an NT_MULTICHAR-packed guid begin with "Hm" for any Hemi applet.
constexpr uint32_t kHemiGuid_Cu = (static_cast<uint32_t>('H')) |
                                  (static_cast<uint32_t>('m') << 8) |
                                  (static_cast<uint32_t>('C') << 16) |
                                  (static_cast<uint32_t>('u') << 24);
constexpr uint32_t kHemiGuid_St = (static_cast<uint32_t>('H')) |
                                  (static_cast<uint32_t>('m') << 8) |
                                  (static_cast<uint32_t>('S') << 16) |
                                  (static_cast<uint32_t>('t') << 24);
constexpr uint32_t kHemiGuid_Re = (static_cast<uint32_t>('H')) |
                                  (static_cast<uint32_t>('m') << 8) |
                                  (static_cast<uint32_t>('R') << 16) |
                                  (static_cast<uint32_t>('e') << 24);
constexpr uint32_t kHemiGuid_Vl = (static_cast<uint32_t>('H')) |
                                  (static_cast<uint32_t>('m') << 8) |
                                  (static_cast<uint32_t>('V') << 16) |
                                  (static_cast<uint32_t>('l') << 24);

const _NT_parameter k_params_3[] = {
    { "Trigger",   0, 1,    0,   kNT_unitNone, 0, nullptr },
    { "Length",    1, 64,   8,   kNT_unitNone, 0, nullptr },
    { "Threshold", 0, 1023, 512, kNT_unitNone, 0, nullptr },
};

// Inject 4 distinct Hemi-prefix algorithms into preset slots 0..3 so the
// host can aggregate one watched lane per preset slot.
void inject_four_hemi_slots() {
    host_proxy::hp_test_clear_slots();
    host_proxy::hp_test_inject_slot(0, "Cumulus",  kHemiGuid_Cu, 3, k_params_3);
    host_proxy::hp_test_inject_slot(1, "Stairs",   kHemiGuid_St, 3, k_params_3);
    host_proxy::hp_test_inject_slot(2, "Relabi",   kHemiGuid_Re, 3, k_params_3);
    host_proxy::hp_test_inject_slot(3, "VectorLF", kHemiGuid_Vl, 3, k_params_3);
}

nt::LoadedPlugin* setup_host() {
    nt::reset_plugin_loader();
    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    return loaded;
}

nt::LoadedPlugin* setup_host_with_four_slots() {
    nt::reset_plugin_loader();
    qq_test_clear_slots();
    inject_four_hemi_slots();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    return loaded;
}

}  // namespace

// ---------------------------------------------------------------------------
// QP1: construct sets numParameters to the proxy-aggregator total (68)
// ---------------------------------------------------------------------------

TEST_CASE("QP1: calculateRequirements reports 68 = 4 selectors + 4*16 proxies",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host();
    _NT_algorithmRequirements req{};
    loaded->factory->calculateRequirements(req, nullptr);
    REQUIRE(req.numParameters == host_proxy::kMaxHostParams);
    REQUIRE(req.numParameters == 68);
    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP2: construct installs 4 enum selectors at parameter indices 0..3
// ---------------------------------------------------------------------------

TEST_CASE("QP2: construct installs 4 selectors at parameters[0..3] bound to enum table",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    const _NT_parameter* params = loaded->algorithm->parameters;
    REQUIRE(params != nullptr);
    for (int lane = 0; lane < 4; ++lane) {
        REQUIRE(params[lane].unit == kNT_unitEnum);
        REQUIRE(params[lane].enumStrings != nullptr);
        REQUIRE(params[lane].min == 0);
        REQUIRE(params[lane].max >= 4);  // at least 4 Hemi entries + "---"
    }
    // Names match "Slot 0".."Slot 3".
    REQUIRE(std::string(params[0].name) == "Slot 0");
    REQUIRE(std::string(params[1].name) == "Slot 1");
    REQUIRE(std::string(params[2].name) == "Slot 2");
    REQUIRE(std::string(params[3].name) == "Slot 3");

    // Enum entry 0 is "---" (unbound); entries 1..4 are the injected applets.
    REQUIRE(std::string(params[0].enumStrings[0]) == "---");
    REQUIRE(std::strstr(params[0].enumStrings[1], "Cumulus")  != nullptr);
    REQUIRE(std::strstr(params[0].enumStrings[2], "Stairs")   != nullptr);
    REQUIRE(std::strstr(params[0].enumStrings[3], "Relabi")   != nullptr);
    REQUIRE(std::strstr(params[0].enumStrings[4], "VectorLF") != nullptr);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP3: construct aggregates each lane against its default-selected slot
// ---------------------------------------------------------------------------

TEST_CASE("QP3: construct aggregates lanes 0..3 against default selector values 1..4",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    const _NT_parameter* params = loaded->algorithm->parameters;
    REQUIRE(params != nullptr);
    // Lane 0 default selector == 1, which resolves to preset slot 0
    // (the Cumulus injection). The proxy region for lane 0 starts at
    // index K = 4 and exposes the 3 vendor params.
    const int base0 = 4;
    REQUIRE(std::strncmp(params[base0 + 0].name, "S0 ", 3) == 0);
    REQUIRE(std::strstr(params[base0 + 0].name, "Trigger") != nullptr);
    REQUIRE(std::strstr(params[base0 + 1].name, "Length") != nullptr);
    REQUIRE(std::strstr(params[base0 + 2].name, "Threshold") != nullptr);

    // Lane 3 default selector == 4, which resolves to preset slot 3 (VectorLF).
    const int base3 = 4 + 3 * host_proxy::kMaxProxyParamsPerSlot;
    REQUIRE(std::strncmp(params[base3 + 0].name, "S3 ", 3) == 0);
    REQUIRE(std::strstr(params[base3 + 0].name, "Trigger") != nullptr);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP4: parameterChanged on a selector reaggregates that lane
// ---------------------------------------------------------------------------

TEST_CASE("QP4: parameterChanged on selector re-aggregates the changed lane",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    // Move lane 2's selector from default (3 = Relabi) to 1 (Cumulus).
    // This writes v[2] then invokes parameterChanged(self, 2).
    NT_setParameterFromUi(NT_algorithmIndex(loaded->algorithm), 2u, 1);

    // After parameterChanged the lane-2 proxy region must be populated
    // from the Cumulus param set (still 3 vendor params, same shape).
    const _NT_parameter* params = loaded->algorithm->parameters;
    const int base2 = 4 + 2 * host_proxy::kMaxProxyParamsPerSlot;
    REQUIRE(std::strncmp(params[base2 + 0].name, "S2 ", 3) == 0);
    REQUIRE(std::strstr(params[base2 + 0].name, "Trigger") != nullptr);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP5: parameterChanged on a selector to "---" clears that lane
// ---------------------------------------------------------------------------

TEST_CASE("QP5: selecting '---' (enum 0) clears the lane's proxy region",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    // Lane 0 currently aggregates Cumulus (3 proxy params). Set v[0] = 0
    // (unbound) and fire parameterChanged.
    NT_setParameterFromUi(NT_algorithmIndex(loaded->algorithm), 0u, 0);

    // host_proxy::aggregate_slot zeroes the lane's _NT_parameter entries
    // (name and enumStrings set to nullptr). Verify the parameter table
    // observable by firmware reflects the cleared lane.
    const _NT_parameter* params = loaded->algorithm->parameters;
    const int base0 = 4;
    REQUIRE(params[base0 + 0].name == nullptr);
    REQUIRE(params[base0 + 0].enumStrings == nullptr);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP6: proxy edit during construct (draw_count == 0) is suppressed
// ---------------------------------------------------------------------------

TEST_CASE("QP6: proxy parameterChanged is suppressed when draw_count == 0",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    // Construct has populated the host's v[] from each parameter's `def`
    // and never invoked draw(). We must NOT see a forward when a proxy
    // index fires parameterChanged at this moment.
    //
    // Verify by stuffing a deliberately-distinct sentinel into the host's
    // own v[] slot we'd write to under a successful forward, calling
    // parameterChanged on a proxy index whose decoded forward target is
    // (slot_idx=0, slot_param_idx=1), and confirming v[1] (the lane-0
    // selector) was NOT overwritten by the forward.
    //
    // Lane 0 default is bound to preset slot 0; the proxy at host index
    // 4+1 (lane-0 vendor param 1 == "Length") decodes to (slot_idx=0,
    // slot_param_idx=1). A successful NT_setParameterFromUi would write
    // value into the host's own v[1]. Stash a sentinel there, fire the
    // proxy callback, and confirm the sentinel survives.

    int16_t* writable_v = const_cast<int16_t*>(loaded->algorithm->v);
    writable_v[1] = 12345;  // sentinel in host's lane-1 selector

    // Fire parameterChanged for proxy index (4 + 1) = host param index 5.
    // We invoke the factory's parameterChanged directly (not through
    // NT_setParameterFromUi) because the latter would itself write the
    // value into v[5] regardless of our guard.
    loaded->factory->parameterChanged(loaded->algorithm, 5);

    // The construct-time guard must have dropped the forward, so v[1]
    // still carries the sentinel.
    REQUIRE(loaded->algorithm->v[1] == 12345);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP7: proxy edit after draw_count > 0 forwards via NT_setParameterFromUi
// ---------------------------------------------------------------------------

TEST_CASE("QP7: proxy parameterChanged forwards after at least one draw",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    // Bind lane 0 to slot 0 (already true by default). Slot index 0 is
    // also the host's own algorithm index in the harness; this lets us
    // observe the forward by reading v[slot_param_idx] after the call.
    //
    // Run one draw() cycle so the host's draw_count > 0 and the proxy
    // forward path is enabled.
    loaded->factory->draw(loaded->algorithm);

    int16_t* writable_v = const_cast<int16_t*>(loaded->algorithm->v);

    // Write the new value to host v[5] (lane-0 proxy[1] = "Length").
    // Then invoke parameterChanged so the host forwards it via
    // NT_setParameterFromUi(slot_idx=0, slot_param_idx=1, v[5]).
    writable_v[5] = 42;
    loaded->factory->parameterChanged(loaded->algorithm, 5);

    // Forward lands at v[1] in the harness's single-slot registry. The
    // recursive parameterChanged(self, 1) is a selector callback that
    // re-aggregates lane 1 against enum value 42, but since 42 is out
    // of range for enum_strs.count, resolve_enum_to_slot returns
    // kInvalidSlotIdx and the lane is cleared. No crash.
    REQUIRE(loaded->algorithm->v[1] == 42);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP8: proxy edit for unbound lane is a no-op (no crash)
// ---------------------------------------------------------------------------

TEST_CASE("QP8: proxy parameterChanged on an unbound lane is a no-op",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    loaded->factory->draw(loaded->algorithm);  // arm draw_count

    // Unbind lane 1 by setting its selector to 0 ("---").
    NT_setParameterFromUi(NT_algorithmIndex(loaded->algorithm), 1u, 0);

    // Lane 1's proxy region starts at K + 1*16 = 20. Firing a proxy
    // callback there must not crash; decode_forward returns invalid.
    int proxy_idx_in_lane_1 = 4 + host_proxy::kMaxProxyParamsPerSlot + 0;
    loaded->factory->parameterChanged(loaded->algorithm, proxy_idx_in_lane_1);
    SUCCEED("no crash");

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP9: focused-slot UX is preserved (button1 still focuses slot 0)
// ---------------------------------------------------------------------------

TEST_CASE("QP9: focused-slot button bindings remain after proxy aggregator install",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    qq_test_set_focused_slot(loaded->algorithm, 3);

    _NT_uiData d{};
    d.controls    = kNT_button1;
    d.lastButtons = 0;
    loaded->factory->customUi(loaded->algorithm, d);

    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 0);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP10: serialise/deserialise still round-trips focused_slot_idx
// ---------------------------------------------------------------------------

TEST_CASE("QP10: serialise/deserialise round-trip preserves focused_slot_idx with proxy state",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host_with_four_slots();
    qq_test_set_focused_slot(loaded->algorithm, 2);

    auto stream = nt::make_json_stream();
    stream->openObject();
    loaded->factory->serialise(loaded->algorithm, *stream);
    stream->closeObject();
    const std::string& json = stream->buffer();
    REQUIRE(!json.empty());

    qq_test_set_focused_slot(loaded->algorithm, 0);
    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 0);

    auto parse = nt::make_json_parse(json);
    bool ok = loaded->factory->deserialise(loaded->algorithm, *parse);
    REQUIRE(ok);
    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 2);

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP11: draw_impl increments draw_count and does not crash with no slots
// ---------------------------------------------------------------------------

TEST_CASE("QP11: draw() increments draw_count and is safe with all slots unbound",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host();  // no hp injected slots
    // No host_proxy slots; refresh_enum_strings yields just "---".
    loaded->factory->draw(loaded->algorithm);
    loaded->factory->draw(loaded->algorithm);

    // After two draws, a proxy forward should not be suppressed by the
    // construct-time guard (draw_count > 0). With no bound lane the
    // decode_forward returns invalid, so the forward is also a no-op.
    int proxy_idx = 4 + 5;
    loaded->factory->parameterChanged(loaded->algorithm, proxy_idx);
    SUCCEED("no crash");

    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QP12: factory metadata unchanged after the proxy aggregator install
// ---------------------------------------------------------------------------

TEST_CASE("QP12: factory metadata is unchanged after proxy aggregator install",
          "[quadrants-host][proxy]") {
    auto* loaded = setup_host();
    REQUIRE(loaded->factory->guid == NT_MULTICHAR('H','m','Q','q'));
    REQUIRE(std::string(loaded->factory->name) == "Quadrants Host");
    REQUIRE(std::string(loaded->factory->description) ==
            "Composes 4 Hemi applets with focused-slot control");
    qq_test_clear_slots();
    host_proxy::hp_test_clear_slots();
}
