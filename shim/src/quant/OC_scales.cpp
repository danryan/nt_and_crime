// Shim port of vendor OC_scales.cpp. Omits SaveToScala and LoadScala
// (dead code on NT: no SD card). Omits avr/pgmspace.h and FLASHMEM
// (not applicable on Cortex-M7 / host sim). Omits the static_assert on
// scale_names count: those string tables are not ported to the shim.
#include "OC_scales.h"
#include "util/util_macros.h"
#include <cstring>

namespace OC {

Scale user_scales[Scales::SCALE_USER_COUNT];
Scale dummy_scale;

void Scales::Init() {
  for (size_t i = 0; i < SCALE_USER_COUNT; ++i)
    memcpy(&user_scales[i], &braids::scales[1], sizeof(Scale));
}

void Scales::Validate() {
  for (size_t i = 0; i < SCALE_USER_COUNT; ++i) {
    CONSTRAIN(user_scales[i].num_notes, 4, 16);
    CONSTRAIN(user_scales[i].span, 12 << 7, 24 << 7);
  }
}

const Scale &Scales::GetScale(int index) {
  CONSTRAIN(index, 0, NUM_SCALES - 1);
  if (index < SCALE_USER_COUNT)
    return user_scales[index];
  else
    return braids::scales[index - SCALE_USER_COUNT];
}

// scale_names, scale_names_short, voltage_scalings are omitted: the NT
// shim has no display subsystem that reads them. Declare the externs so
// code that references them compiles; definitions live here as nullptr stubs.
const char* const scale_names[] = { nullptr };
const char* const scale_names_short[] = { nullptr };
const char* const voltage_scalings[] = { nullptr };

}; // namespace OC
