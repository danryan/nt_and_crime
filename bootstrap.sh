#!/usr/bin/env bash
set -euo pipefail

REQUIRED_BINS=(git make python3 pip3 curl arm-none-eabi-c++ arm-none-eabi-gcc arm-none-eabi-ld arm-none-eabi-objcopy arm-none-eabi-nm arm-none-eabi-objdump)
MISSING=()
for bin in "${REQUIRED_BINS[@]}"; do
    if ! command -v "$bin" >/dev/null 2>&1; then
        MISSING+=("$bin")
    fi
done

# Host C++ compiler: either clang++ or g++ is acceptable
if ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
    MISSING+=("clang++ or g++")
fi

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "bootstrap: missing host-side tools:" >&2
    for m in "${MISSING[@]}"; do echo "  - $m" >&2; done
    echo "Install hints:" >&2
    # xcode-select --install brings make, curl, and git on macOS.
    # gcc-arm-embedded provides the cross-compiler; python3 provides pip3.
    echo "  macOS: xcode-select --install; brew install --cask gcc-arm-embedded; brew install python3" >&2
    echo "  Debian/Ubuntu: apt-get install gcc-arm-none-eabi python3 python3-pip curl make" >&2
    exit 1
fi

# Python deps.
# --user installs into the user site-packages directory (~/.local) rather than
# the system tree, so no root access is needed.
# --break-system-packages is the PEP 668 escape hatch required on Homebrew
# Python 3.11+ and other "managed" installs that forbid global pip writes.
# Older pip versions (< 22.3) do not recognise --break-system-packages, so we
# try without it first and fall back only when the initial attempt fails.
pip3 install --user -r requirements.txt \
    || pip3 install --user --break-system-packages -r requirements.txt

# Vendor sources (distingNT_API, O_C-Phazerville) under vendor/
git submodule update --init --recursive

# Catch2 v3.5.4 single-file amalgamation.
# Guard on BOTH files: if either is absent (e.g. interrupted prior run) we
# re-download both so the harness is never left in a partially-fetched state.
#
# SHA256 of the official v3.5.4 release assets.
# Reverify with: shasum -a 256 catch_amalgamated.hpp catch_amalgamated.cpp
# after fetching from the URL below; expected output:
#   0db5cc809485d3b91debd4679df8fc5ae4197a6ec3faf9292d726f0b078c0199  catch_amalgamated.hpp
#   fa80010aa3fa5e051121f57d8c4174386e1e7b039d7431f3a166462f774be911  catch_amalgamated.cpp
CATCH_HPP_SHA="0db5cc809485d3b91debd4679df8fc5ae4197a6ec3faf9292d726f0b078c0199"
CATCH_CPP_SHA="fa80010aa3fa5e051121f57d8c4174386e1e7b039d7431f3a166462f774be911"
CATCH_BASE_URL="https://github.com/catchorg/Catch2/releases/download/v3.5.4"

verify_sha256() {
    local file="$1"
    local expected="$2"
    local actual
    actual=$(shasum -a 256 "$file" | awk '{print $1}')
    if [ "$actual" != "$expected" ]; then
        echo "bootstrap: SHA256 mismatch for $file" >&2
        echo "  expected: $expected" >&2
        echo "  got:      $actual" >&2
        return 1
    fi
}

if [ ! -f harness/include/catch.hpp ] || [ ! -f harness/src/catch_main.cpp ]; then
    mkdir -p harness/include harness/src

    # Download into temp files first; rename atomically on success so a
    # partial download never leaves a corrupt file in place.
    tmp_hpp=$(mktemp)
    tmp_cpp=$(mktemp)
    # Ensure temp files are removed even on failure
    # shellcheck disable=SC2064
    trap "rm -f '$tmp_hpp' '$tmp_cpp'" EXIT

    curl -sSL -o "$tmp_hpp" "$CATCH_BASE_URL/catch_amalgamated.hpp"
    verify_sha256 "$tmp_hpp" "$CATCH_HPP_SHA"

    curl -sSL -o "$tmp_cpp" "$CATCH_BASE_URL/catch_amalgamated.cpp"
    verify_sha256 "$tmp_cpp" "$CATCH_CPP_SHA"

    mv "$tmp_hpp" harness/include/catch.hpp
    mv "$tmp_cpp" harness/src/catch_main.cpp
fi

# compiler-rt PIC-builtins sanity check. NT firmware does not link
# libgcc / libsupc++ / libstdc++; shim/src/compiler_rt/ vendors the EABI
# 64-bit divide helpers verbatim from llvm-project llvmorg-19.1.0. Verify
# the expected source files are present so make arm does not fail with a
# cryptic missing-include later.
COMPILER_RT_SOURCES=(
    shim/src/compiler_rt/divdi3.c
    shim/src/compiler_rt/udivdi3.c
    shim/src/compiler_rt/divmoddi4.c
    shim/src/compiler_rt/udivmoddi4.c
    shim/src/compiler_rt/arm/aeabi_div0.c
    shim/src/compiler_rt/arm/aeabi_ldivmod.S
    shim/src/compiler_rt/arm/aeabi_uldivmod.S
    shim/src/compiler_rt/int_lib.h
    shim/src/compiler_rt/int_types.h
    shim/src/compiler_rt/int_util.h
    shim/src/compiler_rt/int_endianness.h
    shim/src/compiler_rt/int_div_impl.inc
    shim/src/compiler_rt/assembly.h
)
COMPILER_RT_MISSING=()
for f in "${COMPILER_RT_SOURCES[@]}"; do
    [ -f "$f" ] || COMPILER_RT_MISSING+=("$f")
done
if [ ${#COMPILER_RT_MISSING[@]} -gt 0 ]; then
    echo "bootstrap: missing vendored compiler-rt sources (required for make arm):" >&2
    for m in "${COMPILER_RT_MISSING[@]}"; do echo "  - $m" >&2; done
    echo "Restore from upstream: curl from https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-19.1.0/compiler-rt/lib/builtins/" >&2
    exit 1
fi

# PIC libgcc multilib check. The toolchain should NOT ship a PIC libgcc
# multilib for v7e-m+dp/hard; if it does, vendoring compiler-rt may have
# become optional (but is still preferred for license + maintenance
# reasons). This bullet is sanity verification of continued correctness.
LIBGCC_NOPIC=$(arm-none-eabi-gcc -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -print-libgcc-file-name 2>/dev/null || true)
LIBGCC_PIC=$(arm-none-eabi-gcc -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fPIC -print-libgcc-file-name 2>/dev/null || true)
if [ -n "$LIBGCC_NOPIC" ] && [ -n "$LIBGCC_PIC" ] && [ "$LIBGCC_NOPIC" != "$LIBGCC_PIC" ]; then
    echo "bootstrap: NOTE: arm-none-eabi-gcc reports distinct PIC vs non-PIC libgcc paths." >&2
    echo "  non-PIC: $LIBGCC_NOPIC" >&2
    echo "  PIC:     $LIBGCC_PIC" >&2
    echo "  This is unusual; compiler-rt vendoring remains the preferred path." >&2
fi

echo "bootstrap: OK"
