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
    echo "  macOS: brew install --cask gcc-arm-embedded; brew install python3" >&2
    echo "  Debian/Ubuntu: apt-get install gcc-arm-none-eabi python3 python3-pip curl" >&2
    exit 1
fi

# Python deps
# --break-system-packages is required on macOS Homebrew Python (PEP 668)
# --user keeps the install in ~/.local, not the system tree
pip3 install --user --break-system-packages -r requirements.txt

# Submodules
git submodule update --init --recursive

# Verify Catch2 header presence (downloaded by Task 3 normally; idempotent here)
if [ ! -f harness/include/catch.hpp ]; then
    mkdir -p harness/include harness/src
    curl -sSL -o harness/include/catch.hpp \
        https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.hpp
    curl -sSL -o harness/src/catch_main.cpp \
        https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.cpp
fi

echo "bootstrap: OK"
