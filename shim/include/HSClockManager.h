#pragma once
#include <cstdint>

class HSClockManager {
public:
    bool IsRunning() const { return false; }
    int  GetMultiply(int) const { return 0; }
    uint32_t GetCycleTicks(int) const { return 0; }
};

extern HSClockManager clock_m;
