#!/usr/bin/env bash
# Collapse gcc COMDAT sub-sections into canonical .text/.rodata/.data/.bss.
# NT firmware caps the per-plugin section count; Phase 6 produced 1793 split
# sections vs ~976 in Phase 5. C++ COMDAT for inline class methods emits one
# .text.<mangled> section per method. ld -r + linker scripts do not merge
# COMDAT input sections under partial-link semantics; objcopy --rename-section
# does, by giving every sub-section the same canonical name so the subsequent
# ld -r --gc-keep-exported pass coalesces them.
#
# Usage: merge_sections.sh <input.o> <output.o>
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <input.o> <output.o>" >&2
    exit 2
fi

IN="$1"
OUT="$2"
OBJCOPY=arm-none-eabi-objcopy
READELF=arm-none-eabi-readelf

# Enumerate all sub-sections and build a rename argument list. Use
# `readelf -W -S` because objdump -h truncates long mangled section names.
RENAMES=()
while IFS= read -r name; do
    case "$name" in
        .text._*)            RENAMES+=(--rename-section "$name=.text") ;;
        .rodata._*)          RENAMES+=(--rename-section "$name=.rodata") ;;
        .data._*)            RENAMES+=(--rename-section "$name=.data") ;;
        .data.rel.ro._*)     RENAMES+=(--rename-section "$name=.data.rel.ro") ;;
        .data.rel.local._*)  RENAMES+=(--rename-section "$name=.data.rel.local") ;;
        .bss._*)             RENAMES+=(--rename-section "$name=.bss") ;;
        .gnu.linkonce.t._*)  RENAMES+=(--rename-section "$name=.text") ;;
        .gnu.linkonce.r._*)  RENAMES+=(--rename-section "$name=.rodata") ;;
        .gnu.linkonce.d._*)  RENAMES+=(--rename-section "$name=.data") ;;
    esac
done < <("$READELF" -W -S "$IN" | awk '/^ +\[/{print $2}')

if [ ${#RENAMES[@]} -eq 0 ]; then
    cp "$IN" "$OUT"
else
    "$OBJCOPY" "${RENAMES[@]}" "$IN" "$OUT"
fi
