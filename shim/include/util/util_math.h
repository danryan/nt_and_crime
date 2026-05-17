#pragma once
#include <algorithm>

#ifndef CONSTRAIN
#define CONSTRAIN(x, lo, hi) \
    do { if ((x) < (lo)) (x) = (lo); else if ((x) > (hi)) (x) = (hi); } while (0)
#endif
