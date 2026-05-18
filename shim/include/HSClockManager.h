#pragma once
#include <cstdint>

class HSClockManager {
public:
    bool IsRunning() const { return false; }
    int  GetMultiply(int) const { return 0; }
    uint32_t GetCycleTicks(int) const { return 0; }
    // Called once per inner tick by hemispheres_shim::step(). The current
    // stub is a no-op; dep-clock-mgr's full ClockManager port (Phase 5
    // Layer 1) replaces this class entirely and provides the real
    // tick-advance handler. Layer 0b adds the call site so dep-clock-mgr
    // does not need to touch hemispheres_shim.h.
    void advance_one_tick() {}
};

extern HSClockManager clock_m;
