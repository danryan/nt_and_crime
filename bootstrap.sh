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

# Vendor sources (distingNT_API, O_C-Phazerville) under vendor/.
# vendor/llvm-project is initialized below with a sparse-checkout block;
# init it here only if its .git already exists (worktree re-runs), to
# avoid pulling the full llvm-project tree.
git submodule update --init --recursive vendor/distingNT_API vendor/O_C-Phazerville
if [ -e vendor/llvm-project/.git ]; then
    git submodule update --init vendor/llvm-project
fi

# vendor/llvm-project sparse-checkout: compiler-rt/lib/builtins only.
# The default git submodule update --init --recursive does NOT carry
# sparse-checkout config, so the bootstrap must apply it explicitly.
# Block is idempotent: safe to re-run in fresh clones and in worktrees.
SUBMOD=vendor/llvm-project
URL=$(git config -f .gitmodules submodule.$SUBMOD.url)
TAG=$(git config -f .gitmodules submodule.$SUBMOD.branch)

if [ ! -e "$SUBMOD/.git" ]; then
    git clone --no-checkout --depth=1 --filter=blob:none \
        -b "$TAG" "$URL" "$SUBMOD"
    git -C "$SUBMOD" sparse-checkout init --cone
    git -C "$SUBMOD" sparse-checkout set compiler-rt/lib/builtins
    git -C "$SUBMOD" checkout "$TAG"
    git submodule absorbgitdirs "$SUBMOD"
else
    git -C "$SUBMOD" sparse-checkout reapply
fi

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
# libgcc / libsupc++ / libstdc++; vendor/llvm-project/compiler-rt/lib/builtins/
# (sparse-checkout from llvmorg-19.1.0) provides the EABI 64-bit divide
# helpers. Verify the expected source files are present so make arm does
# not fail with a cryptic missing-include later.
COMPILER_RT_SOURCES=(
    vendor/llvm-project/compiler-rt/lib/builtins/divdi3.c
    vendor/llvm-project/compiler-rt/lib/builtins/udivdi3.c
    vendor/llvm-project/compiler-rt/lib/builtins/divmoddi4.c
    vendor/llvm-project/compiler-rt/lib/builtins/udivmoddi4.c
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_div0.c
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_ldivmod.S
    vendor/llvm-project/compiler-rt/lib/builtins/arm/aeabi_uldivmod.S
    vendor/llvm-project/compiler-rt/lib/builtins/int_lib.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_types.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_util.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_endianness.h
    vendor/llvm-project/compiler-rt/lib/builtins/int_div_impl.inc
    vendor/llvm-project/compiler-rt/lib/builtins/assembly.h
)
COMPILER_RT_MISSING=()
for f in "${COMPILER_RT_SOURCES[@]}"; do
    [ -f "$f" ] || COMPILER_RT_MISSING+=("$f")
done
if [ ${#COMPILER_RT_MISSING[@]} -gt 0 ]; then
    echo "bootstrap: missing compiler-rt sources under vendor/llvm-project (required for make arm):" >&2
    for m in "${COMPILER_RT_MISSING[@]}"; do echo "  - $m" >&2; done
    echo "Confirm sparse-checkout config: git -C vendor/llvm-project sparse-checkout list" >&2
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
    echo "  This is unusual; compiler-rt sourcing from vendor/llvm-project remains the preferred path." >&2
fi

echo "bootstrap: OK"
