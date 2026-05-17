#pragma once
#include <cstdint>

namespace OC {
namespace CORE {
extern volatile uint32_t ticks;  // populated by HemisphereShim each Controller tick
}
}
