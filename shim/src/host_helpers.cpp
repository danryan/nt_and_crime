#include "host_helpers.h"

#include <distingnt/api.h>
#include <distingnt/slot.h>

namespace host_helpers {

ResolvedSlot resolve_slot(uint32_t slot_idx) {
    _NT_slot slot;
    if (!NT_getSlot(slot, slot_idx)) {
        return { nullptr, 0u };
    }
    const uint32_t guid = slot.guid();
    if ((guid & 0xFFFF) != kHemiGuidPrefix) {
        return { nullptr, guid };
    }
    _NT_algorithm* plug = slot.plugin();
    if (!plug) {
        return { nullptr, guid };
    }
    auto* p = static_cast<HemiPluginInterface*>(plug);
    if (p->magic != kHemiInterfaceMagic) {
        return { nullptr, guid };
    }
    if (p->interface_version < kHemiInterfaceVersion) {
        return { nullptr, guid };
    }
    return { p, guid };
}

void render_incompatible_stub(int origin_x, int origin_y) {
    // 64x64 outlined box (1-pixel border).
    NT_drawShapeI(kNT_box, origin_x, origin_y, origin_x + 63, origin_y + 63, 8);
    // "INCOMPATIBLE" centered horizontally at y = origin_y + 30.
    NT_drawText(origin_x + 32, origin_y + 30, "INCOMPATIBLE", 15, kNT_textCentre, kNT_textNormal);
}

}  // namespace host_helpers
