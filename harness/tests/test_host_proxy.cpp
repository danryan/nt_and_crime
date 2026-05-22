// Host-proxy aggregator unit tests.
//
// host_proxy::State is the per-host aggregator that owns the dynamic
// proxy parameter table. The helpers under test (refresh_enum_strings,
// resolve_enum_to_slot, aggregate_slot, decode_forward) are pure
// functions that operate against a State plus the hp_test_inject_slot
// injection table; no firmware NT_* calls happen in this binary.
//
// Spec: docs/superpowers/specs/2026-05-21-host-ux-rework-design.md

#include <cstddef>

#include "catch.hpp"

#include <distingnt/api.h>
#include "host_proxy.h"

#include <cstdint>
#include <cstring>

using host_proxy::State;
using host_proxy::ProxyMap;
using host_proxy::ForwardTarget;
using host_proxy::kInvalidSlotIdx;
using host_proxy::kMaxProxyParamsPerSlot;
using host_proxy::kMaxSlotsPerHost;
using host_proxy::kMaxHemiPrefixEnumEntries;

namespace {

// guid layout: NT_MULTICHAR packs four ASCII chars into a uint32 with the
// first char in the low byte. The host_proxy guid_is_hemi() helper checks
// the low 16 bits == 'm' << 8 | 'H', i.e. the four-char tag begins with "Hm".
constexpr uint32_t kHemiGuid_Cu  = (static_cast<uint32_t>('H')) |
                                   (static_cast<uint32_t>('m') << 8) |
                                   (static_cast<uint32_t>('C') << 16) |
                                   (static_cast<uint32_t>('u') << 24);
constexpr uint32_t kHemiGuid_St  = (static_cast<uint32_t>('H')) |
                                   (static_cast<uint32_t>('m') << 8) |
                                   (static_cast<uint32_t>('S') << 16) |
                                   (static_cast<uint32_t>('1') << 24);
constexpr uint32_t kHemiGuid_Re  = (static_cast<uint32_t>('H')) |
                                   (static_cast<uint32_t>('m') << 8) |
                                   (static_cast<uint32_t>('R') << 16) |
                                   (static_cast<uint32_t>('1') << 24);
constexpr uint32_t kNonHemiGuid  = (static_cast<uint32_t>('g')) |
                                   (static_cast<uint32_t>('G') << 8) |
                                   (static_cast<uint32_t>('a') << 16) |
                                   (static_cast<uint32_t>('n') << 24);

const _NT_parameter sample_params_3[] = {
    { "Trigger",   0, 1,    0, kNT_unitNone, 0, nullptr },
    { "Length",    1, 64,   8, kNT_unitNone, 0, nullptr },
    { "Threshold", 0, 1023, 512, kNT_unitNone, 0, nullptr },
};

const _NT_parameter sample_params_20[] = {
    { "P0",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P1",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P2",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P3",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P4",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P5",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P6",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P7",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P8",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P9",  0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P10", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P11", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P12", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P13", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P14", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P15", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P16", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P17", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P18", 0, 1, 0, kNT_unitNone, 0, nullptr },
    { "P19", 0, 1, 0, kNT_unitNone, 0, nullptr },
};

void clear() {
    host_proxy::hp_test_clear_slots();
}

}  // namespace

TEST_CASE("init populates State with K selector lanes and \"---\" unbound enum entry",
          "[host_proxy][init]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    REQUIRE(s.num_slots == 2);
    REQUIRE(s.kNumSlotIndexParams == 2);
    REQUIRE(s.draw_count == 0);
    REQUIRE(s.enum_strs.count == 1);
    REQUIRE(std::string(s.enum_strs.table[0]) == "---");
    for (int lane = 0; lane < kMaxSlotsPerHost; ++lane) {
        REQUIRE(s.maps[lane].slot_idx == kInvalidSlotIdx);
        REQUIRE(s.maps[lane].slot_param_cnt == 0);
    }
}

TEST_CASE("refresh_enum_strings builds enum_strs from preset scan", "[host_proxy][refresh]") {
    clear();
    State s;
    host_proxy::init(s, 2);

    host_proxy::hp_test_inject_slot(0, "Cumulus",   kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::hp_test_inject_slot(1, "Other",     kNonHemiGuid, 0, nullptr);
    host_proxy::hp_test_inject_slot(2, "Stairs",    kHemiGuid_St, 3, sample_params_3);

    bool changed = host_proxy::refresh_enum_strings(s);
    REQUIRE(changed == true);
    REQUIRE(s.enum_strs.count == 3);
    REQUIRE(std::string(s.enum_strs.table[0]) == "---");
    REQUIRE(std::strstr(s.enum_strs.table[1], "Cumulus") != nullptr);
    REQUIRE(std::strstr(s.enum_strs.table[2], "Stairs") != nullptr);
}

TEST_CASE("refresh_enum_strings returns false when set unchanged", "[host_proxy][refresh]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    REQUIRE(host_proxy::refresh_enum_strings(s) == true);
    REQUIRE(host_proxy::refresh_enum_strings(s) == false);
}

TEST_CASE("resolve_enum_to_slot returns kInvalidSlotIdx for unbound (0)", "[host_proxy][resolve]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(7, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::refresh_enum_strings(s);
    REQUIRE(host_proxy::resolve_enum_to_slot(s, 0) == kInvalidSlotIdx);
}

TEST_CASE("resolve_enum_to_slot maps entry to preset slot index", "[host_proxy][resolve]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(7, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::hp_test_inject_slot(9, "Stairs",  kHemiGuid_St, 3, sample_params_3);
    host_proxy::refresh_enum_strings(s);
    REQUIRE(host_proxy::resolve_enum_to_slot(s, 1) == 7);
    REQUIRE(host_proxy::resolve_enum_to_slot(s, 2) == 9);
}

TEST_CASE("resolve_enum_to_slot returns kInvalidSlotIdx for out-of-range value",
          "[host_proxy][resolve]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::refresh_enum_strings(s);
    REQUIRE(host_proxy::resolve_enum_to_slot(s, 99) == kInvalidSlotIdx);
    REQUIRE(host_proxy::resolve_enum_to_slot(s, -1) == kInvalidSlotIdx);
}

TEST_CASE("aggregate_slot copies vendor params and prefixes names with \"S<lane> \"",
          "[host_proxy][aggregate]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(5, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);

    host_proxy::aggregate_slot(s, 0, 5);

    REQUIRE(s.maps[0].slot_idx == 5);
    REQUIRE(s.maps[0].slot_param_cnt == 3);
    int base = s.kNumSlotIndexParams;  // proxy region starts after K selectors
    REQUIRE(std::strncmp(s.proxy_params[base + 0].name, "S0 ", 3) == 0);
    REQUIRE(std::strstr(s.proxy_params[base + 0].name, "Trigger")   != nullptr);
    REQUIRE(std::strstr(s.proxy_params[base + 1].name, "Length")    != nullptr);
    REQUIRE(std::strstr(s.proxy_params[base + 2].name, "Threshold") != nullptr);
    REQUIRE(s.proxy_params[base + 1].min == 1);
    REQUIRE(s.proxy_params[base + 1].max == 64);
    REQUIRE(s.proxy_params[base + 1].def == 8);
}

TEST_CASE("aggregate_slot clamps to kMaxProxyParamsPerSlot when vendor exposes more",
          "[host_proxy][aggregate]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Big", kHemiGuid_Re, 20, sample_params_20);

    host_proxy::aggregate_slot(s, 0, 0);

    REQUIRE(s.maps[0].slot_param_cnt == kMaxProxyParamsPerSlot);
}

TEST_CASE("aggregate_slot clears lane when called with kInvalidSlotIdx",
          "[host_proxy][aggregate]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);

    host_proxy::aggregate_slot(s, 0, 0);
    REQUIRE(s.maps[0].slot_param_cnt == 3);

    host_proxy::aggregate_slot(s, 0, kInvalidSlotIdx);
    REQUIRE(s.maps[0].slot_idx == kInvalidSlotIdx);
    REQUIRE(s.maps[0].slot_param_cnt == 0);
}

TEST_CASE("aggregate_slot lane 1 places params at offset K + 1*kMaxProxyParamsPerSlot",
          "[host_proxy][aggregate]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);

    host_proxy::aggregate_slot(s, 1, 0);

    int base = s.kNumSlotIndexParams + 1 * kMaxProxyParamsPerSlot;
    REQUIRE(s.maps[1].slot_idx == 0);
    REQUIRE(std::strncmp(s.proxy_params[base + 0].name, "S1 ", 3) == 0);
    REQUIRE(std::strstr(s.proxy_params[base + 0].name, "Trigger") != nullptr);
}

TEST_CASE("init_selector populates proxy_params[lane] with enum binding",
          "[host_proxy][init_selector]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::init_selector(s, 0, "Slot 0", 0);
    host_proxy::init_selector(s, 1, "Slot 1", 0);

    REQUIRE(std::string(s.proxy_params[0].name) == "Slot 0");
    REQUIRE(std::string(s.proxy_params[1].name) == "Slot 1");
    REQUIRE(s.proxy_params[0].min == 0);
    REQUIRE(s.proxy_params[0].max == 0);  // only "---" entry yet
    REQUIRE(s.proxy_params[0].def == 0);
    REQUIRE(s.proxy_params[0].unit == kNT_unitEnum);
    REQUIRE(s.proxy_params[0].enumStrings == s.enum_strs.table);
}

TEST_CASE("init_selector clamps def_value into [0, enum_strs.count - 1]",
          "[host_proxy][init_selector]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::hp_test_inject_slot(1, "Stairs",  kHemiGuid_St, 3, sample_params_3);
    host_proxy::refresh_enum_strings(s);  // enum_strs.count == 3 now

    host_proxy::init_selector(s, 0, "Slot 0", 99);  // out-of-range high
    REQUIRE(s.proxy_params[0].def == 2);            // clamped to count - 1

    host_proxy::init_selector(s, 1, "Slot 1", -5);  // out-of-range low
    REQUIRE(s.proxy_params[1].def == 0);
}

TEST_CASE("refresh_enum_strings auto-updates selector max",
          "[host_proxy][refresh]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::init_selector(s, 0, "Slot 0", 0);
    host_proxy::init_selector(s, 1, "Slot 1", 0);

    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::hp_test_inject_slot(1, "Stairs",  kHemiGuid_St, 3, sample_params_3);
    host_proxy::refresh_enum_strings(s);

    REQUIRE(s.enum_strs.count == 3);
    REQUIRE(s.proxy_params[0].max == 2);
    REQUIRE(s.proxy_params[1].max == 2);

    clear();
    host_proxy::refresh_enum_strings(s);
    REQUIRE(s.enum_strs.count == 1);
    REQUIRE(s.proxy_params[0].max == 0);
    REQUIRE(s.proxy_params[1].max == 0);
}

TEST_CASE("decode_forward maps a host parameter to (slot_idx, slot_param_idx)",
          "[host_proxy][decode]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(7, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::aggregate_slot(s, 0, 7);

    int host_p = s.kNumSlotIndexParams + 1;
    ForwardTarget t = host_proxy::decode_forward(s, host_p);
    REQUIRE(t.slot_idx == 7);
    REQUIRE(t.slot_param_idx == 1);
}

TEST_CASE("decode_forward returns invalid for selector indices",
          "[host_proxy][decode]") {
    clear();
    State s;
    host_proxy::init(s, 2);

    ForwardTarget t0 = host_proxy::decode_forward(s, 0);
    REQUIRE(t0.slot_idx == kInvalidSlotIdx);
    REQUIRE(t0.slot_param_idx == -1);

    ForwardTarget t1 = host_proxy::decode_forward(s, 1);
    REQUIRE(t1.slot_idx == kInvalidSlotIdx);
    REQUIRE(t1.slot_param_idx == -1);
}

TEST_CASE("decode_forward returns invalid when lane is unbound",
          "[host_proxy][decode]") {
    clear();
    State s;
    host_proxy::init(s, 2);

    int host_p = s.kNumSlotIndexParams + 2;
    ForwardTarget t = host_proxy::decode_forward(s, host_p);
    REQUIRE(t.slot_idx == kInvalidSlotIdx);
    REQUIRE(t.slot_param_idx == -1);
}

TEST_CASE("decode_forward returns invalid for host_p beyond the lane's slot_param_cnt",
          "[host_proxy][decode]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::aggregate_slot(s, 0, 0);

    int host_p = s.kNumSlotIndexParams + 5;  // beyond slot_param_cnt = 3
    ForwardTarget t = host_proxy::decode_forward(s, host_p);
    REQUIRE(t.slot_idx == kInvalidSlotIdx);
    REQUIRE(t.slot_param_idx == -1);
}

// Q3 regression: unbound proxy parameters must carry a non-null placeholder
// name so firmware never reads from a null `.name` and prints uninitialized
// memory ("-OFhW" garbage). The placeholder is the static string literal
// "--unused--". Three injection sites in host_proxy.cpp share this contract:
// init() seeds the entire table, aggregate_slot(..., kInvalidSlotIdx) clears
// a lane, and aggregate_slot(..., valid) zero-fills the tail past the slot's
// real param count.

TEST_CASE("init labels every proxy param entry with the --unused-- placeholder",
          "[host_proxy][q3-unused]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    int base = s.kNumSlotIndexParams;  // proxy region starts after K selectors
    for (int p = 0; p < kMaxSlotsPerHost * kMaxProxyParamsPerSlot; ++p) {
        REQUIRE(s.proxy_params[base + p].name != nullptr);
        REQUIRE(std::strcmp(s.proxy_params[base + p].name, "--unused--") == 0);
    }
}

TEST_CASE("aggregate_slot(kInvalidSlotIdx) labels every lane entry --unused--",
          "[host_proxy][q3-unused]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);
    host_proxy::aggregate_slot(s, 0, 0);

    host_proxy::aggregate_slot(s, 0, kInvalidSlotIdx);

    int base = s.kNumSlotIndexParams;
    for (int p = 0; p < kMaxProxyParamsPerSlot; ++p) {
        REQUIRE(s.proxy_params[base + p].name != nullptr);
        REQUIRE(std::strcmp(s.proxy_params[base + p].name, "--unused--") == 0);
    }
}

TEST_CASE("aggregate_slot labels trailing entries past slot_param_cnt --unused--",
          "[host_proxy][q3-unused]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Cumulus", kHemiGuid_Cu, 3, sample_params_3);

    host_proxy::aggregate_slot(s, 0, 0);

    int base = s.kNumSlotIndexParams;
    REQUIRE(s.maps[0].slot_param_cnt == 3);
    for (int p = 3; p < kMaxProxyParamsPerSlot; ++p) {
        REQUIRE(s.proxy_params[base + p].name != nullptr);
        REQUIRE(std::strcmp(s.proxy_params[base + p].name, "--unused--") == 0);
    }
}

TEST_CASE("bind then unbind a lane reverts every entry to --unused--",
          "[host_proxy][q3-unused]") {
    clear();
    State s;
    host_proxy::init(s, 2);
    host_proxy::hp_test_inject_slot(0, "Big", kHemiGuid_Re, 20, sample_params_20);

    host_proxy::aggregate_slot(s, 1, 0);
    int base = s.kNumSlotIndexParams + 1 * kMaxProxyParamsPerSlot;
    // Sanity: lane 1 fully populated to its cap.
    REQUIRE(s.maps[1].slot_param_cnt == kMaxProxyParamsPerSlot);
    REQUIRE(std::strstr(s.proxy_params[base + 0].name, "P0") != nullptr);

    host_proxy::aggregate_slot(s, 1, kInvalidSlotIdx);
    for (int p = 0; p < kMaxProxyParamsPerSlot; ++p) {
        REQUIRE(s.proxy_params[base + p].name != nullptr);
        REQUIRE(std::strcmp(s.proxy_params[base + p].name, "--unused--") == 0);
    }
}
