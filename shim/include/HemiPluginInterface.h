#pragma once

#include <distingnt/api.h>
#include <cstdint>

// Versioned ABI struct stored on each per-applet algorithm instance and
// consumed by host plug-ins (Hemispheres, Quadrants) via the firmware's
// _NT_slot::plugin() pointer. Function pointers are populated by each
// applet's construct(); they are data, not symbols the firmware loader
// resolves at scan time. Hosts validate magic + version before any call.

constexpr uint32_t kHemiInterfaceMagic   = NT_MULTICHAR('H','M','I','1');
constexpr uint32_t kHemiInterfaceVersion = 1;

constexpr uint32_t kHemiGuidPrefix = NT_MULTICHAR('H','m',0,0) & 0xFFFF;

struct HemiPluginInterface : public _NT_algorithm {
    uint32_t magic;
    uint32_t interface_version;
    void (*render_view)(_NT_algorithm* self, int origin_x, int origin_y);
    void (*on_encoder_turn)(_NT_algorithm* self, int direction);
    void (*on_encoder_turn_shifted)(_NT_algorithm* self, int direction);
    void (*on_button_press)(_NT_algorithm* self);
    void (*on_aux_button)(_NT_algorithm* self);
};
