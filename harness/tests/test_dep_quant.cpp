// Output-parity class: integer-only for braids::Quantizer scale lookups
// and round-trips; float-tolerance (1 LSB) for HS:: pitch math at the
// centihertz scale.
// dep-quant placeholder (Phase 5 Layer 0a). The dep-quant implementer
// replaces this with the real Quantizer subsystem invariant test per the
// spec entry. dep-quant is MONOLITHIC per preflight LoC.
#include "catch.hpp"

TEST_CASE("dep-quant placeholder", "[dep-quant]") {
    CHECK(true);
}
