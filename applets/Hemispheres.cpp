#include "hemispheres_shim.h"

NT_HEMISPHERES_PLUGIN("hemi", "Hemispheres",
                     "Phazerville Hemisphere pair: pick two applets")

// Typed applet accessor used by the host test harness. Defined here (rather
// than in the test helper TU) because it needs the complete
// HemispheresInstance type, which only hemispheres_shim.h provides; that
// header can only be included in this single canonical TU to avoid ODR
// link errors from vendor file-scope globals (e.g. hem_MIN, PhzIcons::*,
// _clock_m). The matching declaration lives in
// harness/tests/applet_test_helpers.h. The enum is duplicated rather than
// imported so this file does not depend on the test-only include path.
namespace hem_test {
enum HemSide { LEFT = 0, RIGHT = 1 };
HemisphereApplet* get_applet(hem_shim::HemispheresInstance* hi, HemSide side) {
    return (side == LEFT) ? hi->left : hi->right;
}
}  // namespace hem_test
