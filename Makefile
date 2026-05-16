# nt_and_crime top-level Makefile
.PHONY: help vendor host arm test test-runtime clean deploy

ARM_CXX  := arm-none-eabi-c++
HOST_CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo g++)

NT_API_INCLUDE := vendor/distingNT_API/include
HEM_SRC_DIR    := vendor/O_C-Phazerville/software/src

ARM_FLAGS := -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
             -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall \
             -I$(NT_API_INCLUDE)

HOST_FLAGS := -std=c++14 -fno-rtti -fno-exceptions -Wall -O2 \
              -DNT_HEM_HOST_SIM=1 \
              -Iharness/include -I$(NT_API_INCLUDE)

help:
	@echo "make vendor   - fetch pinned upstream sources via submodules"
	@echo "make arm      - build all NT plug-ins under build/arm/"
	@echo "make host     - build host simulator at build/host/sim"
	@echo "make test     - run all scripted scenarios"
	@echo "make deploy   - copy build/arm/*.o to DEVICE (default: /Volumes/NT)"
	@echo "make clean    - remove build/"

build/host/test_nt_runtime: harness/tests/test_nt_runtime.cpp harness/src/nt_runtime.cpp harness/src/catch_main.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

test-runtime: build/host/test_nt_runtime
	./build/host/test_nt_runtime

vendor:
	git submodule update --init --recursive

clean:
	rm -rf build/
