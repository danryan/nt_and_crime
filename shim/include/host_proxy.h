#pragma once

#include <cstddef>
#include <cstdint>
#include <distingnt/api.h>

// Shared proxy aggregator for per-applet host plug-ins (Hemispheres_host,
// Quadrants_host). The host's parameter table is dynamically populated
// from each watched preset slot's _NT_parameter[] table at construct
// time, plus selector enums that scroll through Hemi-prefix algorithms.
//
// Two layers:
//
//   - Selector parameters (host->v[0..K-1]): one per watched slot lane;
//     each value is an enum index into State::enum_strs.table.
//   - Proxy parameters  (host->v[K..]):       slot-local _NT_parameter
//     copies, prefixed "S<lane> " in the visible name.
//
// Wiring contract on the host side (see spec):
//
//   construct_impl:    initialize State, populate selectors, scan preset
//                      for Hemi-prefix algorithms (refresh_enum_strings),
//                      aggregate each lane (aggregate_slot).
//   draw_impl:         increment State::draw_count once per draw; call
//                      refresh_enum_strings opportunistically.
//   parameterChanged:  if host_p < kNumSlotIndexParams refresh enum +
//                      reaggregate that lane; else if draw_count > 0
//                      decode_forward + NT_setParameterFromUi.
//
// Construct-time guard (draw_count == 0) suppresses the spurious
// parameterChanged firings firmware emits during construct (the inner
// NT_setParameterFromUi back into self at that moment hard-crashes
// the device; see CLAUDE.md "Construct-time parameterChanged hazard").

namespace host_proxy {

inline constexpr int      kMaxSlotsPerHost          = 4;
inline constexpr int      kMaxProxyParamsPerSlot    = 16;
inline constexpr int      kMaxHemiPrefixEnumEntries = 32;
inline constexpr uint32_t kInvalidSlotIdx           = 0xFFFFFFFFu;
inline constexpr int      kProxyNameMax             = 24;
inline constexpr int      kEnumNameMax              = 20;

struct EnumStrings {
    char  storage[kMaxHemiPrefixEnumEntries][kEnumNameMax];
    const char* table[kMaxHemiPrefixEnumEntries];
    int   count;
};

struct ProxyMap {
    uint32_t slot_idx;        // resolved preset slot index, or kInvalidSlotIdx
    int      slot_param_cnt;  // 0..kMaxProxyParamsPerSlot
};

struct State {
    int            num_slots;          // K
    int            kNumSlotIndexParams; // K, copied for readers
    EnumStrings    enum_strs;
    _NT_parameter  proxy_params[kMaxSlotsPerHost * kMaxProxyParamsPerSlot];
    char           proxy_names[kMaxSlotsPerHost * kMaxProxyParamsPerSlot][kProxyNameMax];
    ProxyMap       maps[kMaxSlotsPerHost];
    uint32_t       draw_count;
};

struct ForwardTarget {
    uint32_t slot_idx;
    int      slot_param_idx;
};

// Initialize a State for a host with K = num_slots selector lanes.
// Zeros all proxy fields, sets num_slots / kNumSlotIndexParams, builds the
// initial enum table containing only the "---" unbound entry.
void init(State& s, int num_slots);

// Rebuild State::enum_strs by scanning NT_algorithmCount() preset slots
// for guid prefix 'Hm'. Index 0 is reserved for "---" (unbound).
// Returns true if the enum set changed (caller can fire
// NT_updateParameterDefinition for each selector parameter).
bool refresh_enum_strings(State& s);

// Resolve an enum value (0 = unbound, 1.. = entry in enum_strs.table)
// to a preset slot index. Returns kInvalidSlotIdx when unbound or oob.
uint32_t resolve_enum_to_slot(const State& s, int enum_value);

// Aggregate one watched slot's _NT_parameter[] entries into proxy_params
// and proxy_names starting at lane * kMaxProxyParamsPerSlot. Stores the
// resolved slot_idx and slot_param_cnt into State::maps[lane].
// slot_idx == kInvalidSlotIdx clears the lane.
// Vendor name truncates to kProxyNameMax - 4 chars (prefix "S<d> ").
void aggregate_slot(State& s, int lane, uint32_t slot_idx);

// Map a host parameter index to a forward target. Returns
// { kInvalidSlotIdx, -1 } for selector indices (< kNumSlotIndexParams)
// and for indices outside any aggregated lane.
ForwardTarget decode_forward(const State& s, int host_p);

#ifdef NT_HEM_HOST_SIM
// Host-test injection seam. Tests populate this table before invoking
// helpers; refresh_enum_strings and aggregate_slot read from it instead
// of calling NT_* firmware functions. Mirrors the *_test_inject_slot
// pattern used by Hemispheres_host and Quadrants_host.
extern "C" void hp_test_inject_slot(uint32_t slot_idx,
                                    const char* name,
                                    uint32_t guid,
                                    int num_params,
                                    const _NT_parameter* params);
extern "C" void hp_test_clear_slots(void);
#endif

}  // namespace host_proxy
