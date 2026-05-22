// Host-proxy aggregator implementation.
//
// See shim/include/host_proxy.h for the contract. See the spec at
// docs/superpowers/specs/2026-05-21-host-ux-rework-design.md for design
// rationale.
//
// Two execution paths exist for refresh_enum_strings and aggregate_slot:
//
//   - Default (firmware): walk NT_algorithmCount() / NT_getSlot().
//   - NT_HEM_HOST_SIM:    read the in-process s_test_slots[] table,
//                         populated by hp_test_inject_slot(). The host
//                         test binary defines NT_HEM_HOST_SIM and these
//                         helpers therefore never call into the firmware
//                         ABI. Mirrors the existing *_test_inject_slot
//                         pattern used by Hemispheres_host / Quadrants_host.

#include "host_proxy.h"

#include <cstdio>
#include <cstring>

#ifndef NT_HEM_HOST_SIM
#include <distingnt/slot.h>
#endif

namespace host_proxy {

namespace {

// Hemi guid prefix matches HemiPluginInterface::kHemiGuidPrefix: low 16
// bits of the guid form 'Hm' under NT_MULTICHAR's little-endian packing.
constexpr uint32_t kHemiPrefixMask = 0xFFFFu;
constexpr uint32_t kHemiPrefix     = static_cast<uint32_t>('H') |
                                     (static_cast<uint32_t>('m') << 8);

bool guid_is_hemi(uint32_t guid) {
    return (guid & kHemiPrefixMask) == kHemiPrefix;
}

void clamp_copy(char* dst, size_t cap, const char* src) {
    if (cap == 0) return;
    size_t n = 0;
    while (n + 1 < cap && src && src[n]) {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

}  // namespace

#ifdef NT_HEM_HOST_SIM

namespace {

struct TestSlotEntry {
    bool                  valid;
    char                  name[kEnumNameMax];
    uint32_t              guid;
    int                   num_params;
    const _NT_parameter*  params;
};

constexpr int kTestSlotCap = 32;
TestSlotEntry s_test_slots[kTestSlotCap];

}  // namespace

extern "C" void hp_test_inject_slot(uint32_t slot_idx,
                                    const char* name,
                                    uint32_t guid,
                                    int num_params,
                                    const _NT_parameter* params) {
    if (slot_idx >= static_cast<uint32_t>(kTestSlotCap)) return;
    TestSlotEntry& e = s_test_slots[slot_idx];
    e.valid      = true;
    clamp_copy(e.name, sizeof(e.name), name);
    e.guid       = guid;
    e.num_params = num_params;
    e.params     = params;
}

extern "C" void hp_test_clear_slots(void) {
    for (int i = 0; i < kTestSlotCap; ++i) {
        s_test_slots[i] = { false, {0}, 0u, 0, nullptr };
    }
}

namespace {

uint32_t test_algorithm_count() {
    uint32_t hi = 0;
    for (int i = 0; i < kTestSlotCap; ++i) {
        if (s_test_slots[i].valid) hi = static_cast<uint32_t>(i + 1);
    }
    return hi;
}

bool test_get_name_guid(uint32_t idx, char* name_out, size_t cap, uint32_t* guid_out) {
    if (idx >= static_cast<uint32_t>(kTestSlotCap)) return false;
    const TestSlotEntry& e = s_test_slots[idx];
    if (!e.valid) return false;
    clamp_copy(name_out, cap, e.name);
    *guid_out = e.guid;
    return true;
}

bool test_get_params(uint32_t idx, int* count_out, const _NT_parameter** params_out) {
    if (idx >= static_cast<uint32_t>(kTestSlotCap)) return false;
    const TestSlotEntry& e = s_test_slots[idx];
    if (!e.valid) return false;
    *count_out  = e.num_params;
    *params_out = e.params;
    return true;
}

}  // namespace

#else  // !NT_HEM_HOST_SIM

namespace {

uint32_t test_algorithm_count() {
    return NT_algorithmCount();
}

bool test_get_name_guid(uint32_t idx, char* name_out, size_t cap, uint32_t* guid_out) {
    _NT_slot slot;
    if (!NT_getSlot(slot, idx)) return false;
    clamp_copy(name_out, cap, slot.name());
    *guid_out = slot.guid();
    return true;
}

bool test_get_params(uint32_t idx, int* count_out, const _NT_parameter** /*params_out*/) {
    _NT_slot slot;
    if (!NT_getSlot(slot, idx)) return false;
    *count_out = static_cast<int>(slot.numParameters());
    // For firmware path the caller fetches each parameter via
    // slot.parameterInfo(); aggregate_slot handles that branch separately.
    return true;
}

bool firmware_get_param_info(uint32_t idx, int p, _NT_parameter* info_out) {
    _NT_slot slot;
    if (!NT_getSlot(slot, idx)) return false;
    return slot.parameterInfo(*info_out, static_cast<uint32_t>(p));
}

}  // namespace

#endif  // NT_HEM_HOST_SIM

void init(State& s, int num_slots) {
    if (num_slots < 0) num_slots = 0;
    if (num_slots > kMaxSlotsPerHost) num_slots = kMaxSlotsPerHost;
    s.num_slots           = num_slots;
    s.kNumSlotIndexParams = num_slots;
    s.draw_count          = 0;

    for (int i = 0; i < kMaxHemiPrefixEnumEntries; ++i) {
        s.enum_strs.storage[i][0] = '\0';
        s.enum_strs.table[i]      = s.enum_strs.storage[i];
    }
    clamp_copy(s.enum_strs.storage[0], kEnumNameMax, "---");
    s.enum_strs.count = 1;

    for (int i = 0; i < kMaxSlotsPerHost * kMaxProxyParamsPerSlot; ++i) {
        s.proxy_params[i] = { nullptr, 0, 0, 0, kNT_unitNone, 0, nullptr };
        s.proxy_names[i][0] = '\0';
    }
    for (int lane = 0; lane < kMaxSlotsPerHost; ++lane) {
        s.maps[lane].slot_idx       = kInvalidSlotIdx;
        s.maps[lane].slot_param_cnt = 0;
    }
}

bool refresh_enum_strings(State& s) {
    char next_storage[kMaxHemiPrefixEnumEntries][kEnumNameMax];
    int  next_count = 0;

    clamp_copy(next_storage[next_count], kEnumNameMax, "---");
    ++next_count;

    const uint32_t n = test_algorithm_count();
    for (uint32_t i = 0; i < n; ++i) {
        if (next_count >= kMaxHemiPrefixEnumEntries) break;
        char     name[kEnumNameMax];
        uint32_t guid = 0;
        if (!test_get_name_guid(i, name, sizeof(name), &guid)) continue;
        if (!guid_is_hemi(guid)) continue;
        char* dst = next_storage[next_count];
        char  prefix[6];
        std::snprintf(prefix, sizeof(prefix), "%u ", static_cast<unsigned>(i));
        size_t off = 0;
        while (off + 1 < kEnumNameMax && prefix[off]) {
            dst[off] = prefix[off];
            ++off;
        }
        for (size_t k = 0; off + 1 < kEnumNameMax && name[k]; ++k, ++off) {
            dst[off] = name[k];
        }
        dst[off] = '\0';
        ++next_count;
    }

    bool changed = (next_count != s.enum_strs.count);
    if (!changed) {
        for (int i = 0; i < next_count; ++i) {
            if (std::strcmp(s.enum_strs.storage[i], next_storage[i]) != 0) {
                changed = true;
                break;
            }
        }
    }

    for (int i = 0; i < next_count; ++i) {
        clamp_copy(s.enum_strs.storage[i], kEnumNameMax, next_storage[i]);
    }
    for (int i = next_count; i < kMaxHemiPrefixEnumEntries; ++i) {
        s.enum_strs.storage[i][0] = '\0';
    }
    s.enum_strs.count = next_count;
    for (int i = 0; i < kMaxHemiPrefixEnumEntries; ++i) {
        s.enum_strs.table[i] = s.enum_strs.storage[i];
    }
    // resolve_enum_to_slot re-scans the preset to map enum -> preset slot
    // idx; refresh + resolve walk the same preset-scan order so the Nth
    // Hemi-prefix algorithm corresponds to enum_value == N. No side table
    // needed on State.
    return changed;
}

uint32_t resolve_enum_to_slot(const State& s, int enum_value) {
    if (enum_value <= 0) return kInvalidSlotIdx;
    if (enum_value >= s.enum_strs.count) return kInvalidSlotIdx;
    // Re-scan preset to map enum_value (1..N) to its preset slot index.
    // refresh_enum_strings populates enum entries in preset-scan order so the
    // Nth Hemi-prefix algorithm corresponds to enum_value == N.
    int kept = 0;
    const uint32_t n = test_algorithm_count();
    for (uint32_t i = 0; i < n; ++i) {
        char     name[kEnumNameMax];
        uint32_t guid = 0;
        if (!test_get_name_guid(i, name, sizeof(name), &guid)) continue;
        if (!guid_is_hemi(guid)) continue;
        ++kept;
        if (kept == enum_value) return i;
    }
    return kInvalidSlotIdx;
}

namespace {

void format_proxy_name(char* dst, int lane, const char* vendor_name) {
    char prefix[5];
    std::snprintf(prefix, sizeof(prefix), "S%d ", lane);
    size_t off = 0;
    while (off + 1 < kProxyNameMax && prefix[off]) {
        dst[off] = prefix[off];
        ++off;
    }
    if (vendor_name) {
        for (size_t k = 0; off + 1 < kProxyNameMax && vendor_name[k]; ++k, ++off) {
            dst[off] = vendor_name[k];
        }
    }
    dst[off] = '\0';
}

}  // namespace

void aggregate_slot(State& s, int lane, uint32_t slot_idx) {
    if (lane < 0 || lane >= kMaxSlotsPerHost) return;
    int base = lane * kMaxProxyParamsPerSlot;

    if (slot_idx == kInvalidSlotIdx) {
        s.maps[lane].slot_idx       = kInvalidSlotIdx;
        s.maps[lane].slot_param_cnt = 0;
        for (int p = 0; p < kMaxProxyParamsPerSlot; ++p) {
            s.proxy_params[base + p] = { nullptr, 0, 0, 0, kNT_unitNone, 0, nullptr };
            s.proxy_names[base + p][0] = '\0';
        }
        return;
    }

#ifdef NT_HEM_HOST_SIM
    int                  count_in  = 0;
    const _NT_parameter* params_in = nullptr;
    if (!test_get_params(slot_idx, &count_in, &params_in)) {
        s.maps[lane].slot_idx       = kInvalidSlotIdx;
        s.maps[lane].slot_param_cnt = 0;
        return;
    }
    int count = count_in;
    if (count > kMaxProxyParamsPerSlot) count = kMaxProxyParamsPerSlot;
    for (int p = 0; p < count; ++p) {
        s.proxy_params[base + p]      = params_in[p];
        format_proxy_name(s.proxy_names[base + p], lane, params_in[p].name);
        s.proxy_params[base + p].name = s.proxy_names[base + p];
    }
    for (int p = count; p < kMaxProxyParamsPerSlot; ++p) {
        s.proxy_params[base + p] = { nullptr, 0, 0, 0, kNT_unitNone, 0, nullptr };
        s.proxy_names[base + p][0] = '\0';
    }
    s.maps[lane].slot_idx       = slot_idx;
    s.maps[lane].slot_param_cnt = count;
#else
    int count = 0;
    const _NT_parameter* unused = nullptr;
    if (!test_get_params(slot_idx, &count, &unused)) {
        s.maps[lane].slot_idx       = kInvalidSlotIdx;
        s.maps[lane].slot_param_cnt = 0;
        return;
    }
    if (count > kMaxProxyParamsPerSlot) count = kMaxProxyParamsPerSlot;
    for (int p = 0; p < count; ++p) {
        _NT_parameter info {};
        if (!firmware_get_param_info(slot_idx, p, &info)) {
            count = p;
            break;
        }
        s.proxy_params[base + p]      = info;
        format_proxy_name(s.proxy_names[base + p], lane, info.name);
        s.proxy_params[base + p].name = s.proxy_names[base + p];
    }
    for (int p = count; p < kMaxProxyParamsPerSlot; ++p) {
        s.proxy_params[base + p] = { nullptr, 0, 0, 0, kNT_unitNone, 0, nullptr };
        s.proxy_names[base + p][0] = '\0';
    }
    s.maps[lane].slot_idx       = slot_idx;
    s.maps[lane].slot_param_cnt = count;
#endif
}

ForwardTarget decode_forward(const State& s, int host_p) {
    if (host_p < s.kNumSlotIndexParams) return { kInvalidSlotIdx, -1 };
    int rel  = host_p - s.kNumSlotIndexParams;
    int lane = rel / kMaxProxyParamsPerSlot;
    int p    = rel % kMaxProxyParamsPerSlot;
    if (lane < 0 || lane >= s.num_slots) return { kInvalidSlotIdx, -1 };
    const ProxyMap& m = s.maps[lane];
    if (m.slot_idx == kInvalidSlotIdx) return { kInvalidSlotIdx, -1 };
    if (p >= m.slot_param_cnt) return { kInvalidSlotIdx, -1 };
    return { m.slot_idx, p };
}

}  // namespace host_proxy
