#include "HemisphereApplet.h"
#include "Logic.h"

// Logic adapter translation unit. Vendor header included here for per-applet
// compile isolation: vendor-side errors localize to this TU. Multiple TUs
// (this adapter plus Hemispheres_main.o) emit identical vendor helpers
// (hem_XOR, hem_NAND, LOGIC_ICON); ld -r uses --allow-multiple-definition
// to dedup. NT-specific Logic glue (icon overrides, helpers) goes here in
// future work.
