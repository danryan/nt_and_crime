#include "hemispheres_shim.h"

NT_HEMISPHERES_PLUGIN("hemi", "Hemispheres",
                     "Phazerville Hemisphere pair: pick two applets")

// Test seam: define get_applet_impl here so it can dereference
// HemispheresInstance fields. Declared in harness/tests/applet_test_helpers.h.
// Takes int (0=LEFT, 1=RIGHT) to avoid duplicating hem_test::HemSide in this TU.
namespace hem_test {
HemisphereApplet* get_applet_impl(hem_shim::HemispheresInstance* hi, int side) {
    return (side == 0) ? hi->left : hi->right;
}
}  // namespace hem_test
