#pragma once
// Vendor app: (none -- throwaway foundation stub)
//
// O_C-app manifest. Mirrors the per-applet manifest shape
// (shim/include/applet_manifests/<APPLET>.h) but describes a full-screen O_C
// app rather than a Hemisphere applet: it declares 4 CV inputs, 4 CV outputs,
// and 4 trigger inputs, matching the fixed I/O routing block the per-app
// runtime emits (oc_runtime::emit_io_params). The guid uses an "OC" prefix so
// it never collides with the Hemisphere "Hm" space or the composer host guids
// (HmHh / QdHh).
#include "../applet_manifest.h"
#include <distingnt/api.h>

namespace oc_app {
struct StubApp {
    static constexpr uint32_t    guid        = NT_MULTICHAR('O', 'C', 'S', 'b');
    static constexpr const char* name        = "OC Stub";
    static constexpr const char* description = "Foundation stub proving the O_C-app build path";

    // I/O port lists. The per-app runtime emits a fixed 12-row routing block
    // (4 CV in + 4 CV out + 4 TR in); these names document the intent and
    // give a real app a place to override the labels later.
    static constexpr BusParam inputs[] = {
        {"CV in 1", BusKind::cv}, {"CV in 2", BusKind::cv},
        {"CV in 3", BusKind::cv}, {"CV in 4", BusKind::cv},
    };
    static constexpr BusParam outputs[] = {
        {"CV out 1", BusKind::cv}, {"CV out 2", BusKind::cv},
        {"CV out 3", BusKind::cv}, {"CV out 4", BusKind::cv},
    };
    static constexpr BusParam triggers[] = {
        {"TR in 1", BusKind::gate}, {"TR in 2", BusKind::gate},
        {"TR in 3", BusKind::gate}, {"TR in 4", BusKind::gate},
    };
};
}  // namespace oc_app
