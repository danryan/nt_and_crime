#!/usr/bin/env bash
set -euo pipefail

REQUIRED_BINS=(git make python3 pip3 curl arm-none-eabi-c++)
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
# SHA256 constants below are placeholders pending verification against the
# official release assets on a known-good network.
# TODO(bootstrap): replace with verified SHAs obtained via:
#   shasum -a 256 catch_amalgamated.hpp catch_amalgamated.cpp
CATCH_HPP_SHA="0000000000000000000000000000000000000000000000000000000000000000"
CATCH_CPP_SHA="0000000000000000000000000000000000000000000000000000000000000000"
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

echo "bootstrap: OK"
