#!/usr/bin/env bash
# Usage: probe_applet.sh <AppletName>
# Tries to compile a TU that includes the vendor applet header through the shim
# and instantiates the class once, so the compiler resolves all references.
set -euo pipefail

NAME="${1:?usage: probe_applet.sh <AppletName>}"

TMP="$(mktemp -t probe_${NAME}.XXXXXX.cpp)"
trap "rm -f '$TMP'" EXIT

cat > "$TMP" <<EOF
#include "HemisphereApplet.h"
#include "${NAME}.h"

// Force instantiation so every reachable inline definition is checked.
namespace { ${NAME} _probe_${NAME}; }
EOF

arm-none-eabi-c++ -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
    -mthumb -fno-rtti -fno-exceptions -fno-threadsafe-statics \
    -Os -fPIC -Wall \
    -Ivendor/distingNT_API/include \
    -Ishim/include \
    -Ivendor/O_C-Phazerville/software/src/applets \
    -c -o /dev/null "$TMP"

echo "probe: ${NAME} compiles clean"
