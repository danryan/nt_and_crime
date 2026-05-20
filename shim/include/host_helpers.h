#pragma once

#include <cstddef>            // for NULL (used by distingnt/slot.h)
#include <distingnt/api.h>
#include <distingnt/slot.h>
#include "HemiPluginInterface.h"

namespace host_helpers {

struct ResolvedSlot {
    HemiPluginInterface* plugin;  // nullptr when the slot did not validate.
    uint32_t             guid;    // 0 when the slot is empty.
};

// Resolve a slot index to a validated HemiPluginInterface pointer.
// Validation checks: NT_getSlot succeeds, guid prefix is 'Hm', plugin()
// is non-null, magic equals kHemiInterfaceMagic, version is at least
// kHemiInterfaceVersion. Any failure yields ResolvedSlot{nullptr, 0}.
//
// Hosts MUST cache the result per draw cycle: call once per slot per
// draw or event, route through the cached plugin pointer if non-null,
// otherwise render the incompatible stub at the slot's origin.
ResolvedSlot resolve_slot(uint32_t slot_idx);

// Render the 64x64 incompatible-slot indicator at (origin_x, origin_y).
// Used by every host when resolve_slot returns {nullptr, *}. Same
// routine for Hemispheres and Quadrants so the visual is consistent.
void render_incompatible_stub(int origin_x, int origin_y);

}  // namespace host_helpers
