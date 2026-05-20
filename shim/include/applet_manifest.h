#pragma once

#include <cstdint>

// Shared types consumed by each per-applet manifest in
// shim/include/applet_manifests/<APPLET>.h. Manifests declare the applet's
// guid, name, description, and bus-port lists; the per-applet runtime
// helpers consume the lists to build the _NT_parameter table and to drive
// bus I/O without per-applet boilerplate.

enum class BusKind : uint8_t {
    gate,
    cv,
    audio,
};

struct BusParam {
    const char* name;
    BusKind     kind;
};
