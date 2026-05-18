# nt_and_crime top-level Makefile
.PHONY: help vendor host arm test test-runtime clean deploy deploy-sysex

ARM_CXX  := arm-none-eabi-c++
HOST_CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo g++)

NT_API_INCLUDE := vendor/distingNT_API/include
HEM_SRC_DIR    := vendor/O_C-Phazerville/software/src

ARM_FLAGS := -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
             -mthumb -fno-rtti -fno-exceptions -fno-threadsafe-statics \
             -Os -fPIC -Wall \
             -I$(NT_API_INCLUDE)

HOST_FLAGS := -std=c++14 -fno-rtti -fno-exceptions -Wall -O2 \
              -DNT_HEM_HOST_SIM=1 \
              -Iharness/include -I$(NT_API_INCLUDE)

help:
	@echo "make vendor   - fetch pinned upstream sources via submodules"
	@echo "make arm      - build all NT plug-ins under build/arm/"
	@echo "make host     - build host simulator at build/host/sim_gainCustomUI"
	@echo "make test     - run all scripted scenarios"
	@echo "make test-applets - run host Catch2 binary for Hemispheres applet logic"
	@echo "make deploy        - copy build/arm/*.o to DEVICE/programs/plug-ins/ (default DEVICE: /Volumes/NT; NT must be in USB disk mode)"
	@echo "make deploy-sysex  - push build/arm/Hemispheres.o via USB-MIDI sysex (NT firmware v1.13+, no reboot)"
	@echo "make clean    - remove build/"

# Sources shared by every host build (no Catch2 main).
HARNESS_LIB_SRCS := harness/src/nt_runtime.cpp \
                    harness/src/font_placeholder.cpp \
                    harness/src/nt_jsonstream.cpp \
                    harness/src/plugin_loader.cpp

# Test builds add catch_main.cpp which supplies Catch2's main().
HARNESS_SRCS := $(HARNESS_LIB_SRCS) harness/src/catch_main.cpp

build/host/test_nt_runtime: harness/tests/test_nt_runtime.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

test-runtime: build/host/test_nt_runtime
	./build/host/test_nt_runtime

build/host/test_buses: harness/tests/test_buses.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

test-buses: build/host/test_buses
	./build/host/test_buses

build/host/test_draw_text: harness/tests/test_draw_text.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

.PHONY: test-draw
test-draw: build/host/test_draw_text
	./build/host/test_draw_text

build/host/test_draw_shape: harness/tests/test_draw_shape.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

.PHONY: test-draw-shape
test-draw-shape: build/host/test_draw_shape
	./build/host/test_draw_shape

build/host/test_json: harness/tests/test_json.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

.PHONY: test-json
test-json: build/host/test_json
	./build/host/test_json

build/host/test_params: harness/tests/test_params.cpp harness/src/plugin_loader.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

.PHONY: test-params
test-params: build/host/test_params
	./build/host/test_params

build/host/test_loader: harness/tests/test_loader.cpp \
                        vendor/distingNT_API/examples/gainCustomUI.cpp \
                        $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

.PHONY: test-loader
test-loader: build/host/test_loader
	./build/host/test_loader

build/host/sim_gainCustomUI: $(HARNESS_LIB_SRCS) vendor/distingNT_API/examples/gainCustomUI.cpp harness/src/main.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ $^

.PHONY: host
host: build/host/sim_gainCustomUI

ARM_REF_SRCS := vendor/distingNT_API/examples/gainCustomUI.cpp \
                vendor/distingNT_API/examples/gain.cpp

build/arm/gainCustomUI.o: vendor/distingNT_API/examples/gainCustomUI.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<

build/arm/gain.o: vendor/distingNT_API/examples/gain.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<

build/arm/bus_probe.o: applets/bus_probe.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<

build/arm/aeabi_probe.o: applets/aeabi_probe.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<

# Hem shim sources (header-only for now; compiled as part of each applet's TU)
SHIM_INCLUDE := -Ishim/include
HEM_APPLET_INCLUDE := -Ivendor/O_C-Phazerville/software/src/applets

SHIM_DEPS := $(wildcard shim/include/*.h) $(wildcard shim/include/*/*.h) $(wildcard shim/src/*.cpp)

build/arm/Hemispheres.o: applets/Hemispheres.cpp $(SHIM_DEPS) | build/arm/libgcc_parts.stamp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o build/arm/Hemispheres.raw.o $<
	$(ARM_LD) -r --strip-debug build/arm/Hemispheres.raw.o build/arm/libgcc_parts/*.o -o build/arm/Hemispheres.linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.comment' build/arm/Hemispheres.linked.o $@

# Extract the libgcc helper objects the NT firmware does not provide
# (__aeabi_ldivmod, __aeabi_uldivmod, primitives, divide-by-zero handler).
# Firmware confirmed-missing via applets/aeabi_probe.cpp deployment 2026-05-18.
# Partial-linking these into each plug-in .o resolves the symbols on-device.
ARM_LD := arm-none-eabi-ld
LIBGCC_PATH := $(shell $(ARM_CXX) -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -print-libgcc-file-name)
LIBGCC_MEMBERS := _aeabi_ldivmod.o _aeabi_uldivmod.o _divmoddi4.o _udivmoddi4.o _dvmd_tls.o

build/arm/libgcc_parts.stamp: $(LIBGCC_PATH)
	mkdir -p build/arm/libgcc_parts
	cd build/arm/libgcc_parts && arm-none-eabi-ar x $(LIBGCC_PATH) $(LIBGCC_MEMBERS)
	touch $@

build/host/Hemispheres.host.o: applets/Hemispheres.cpp $(SHIM_DEPS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<

build/host/test_hemispheres: harness/tests/test_hemispheres.cpp harness/tests/applet_test_helpers.cpp build/host/Hemispheres.host.o $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-applets
test-applets: build/host/test_hemispheres
	./build/host/test_hemispheres

# Phase 5 dep tests. Each per-dep Catch2 binary builds against the same
# harness shim as test_hemispheres but is standalone so a single dep test
# can run in isolation. test-deps runs all 6.
DEP_TESTS := test_dep_vec_osc test_dep_lorenz test_dep_tideslite \
             test_dep_clock_mgr test_dep_quant test_dep_cv_map

build/host/test_dep_%: harness/tests/test_dep_%.cpp build/host/Hemispheres.host.o $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-deps
test-deps: $(addprefix build/host/, $(DEP_TESTS))
	@for t in $^; do echo "Running $$t"; ./$$t || exit 1; done

arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/aeabi_probe.o build/arm/Hemispheres.o

DEVICE ?= /Volumes/NT
PLUGIN_DIR := programs/plug-ins
deploy: arm
	@test -d "$(DEVICE)" || { echo "DEVICE=$(DEVICE) not mounted. Put the NT in USB disk mode via Misc menu first."; exit 1; }
	@mkdir -p "$(DEVICE)/$(PLUGIN_DIR)"
	cp build/arm/*.o "$(DEVICE)/$(PLUGIN_DIR)/"
	@echo "Deployed to $(DEVICE)/$(PLUGIN_DIR)/. Eject the volume, then press both encoders together on the NT to reboot into normal mode."

# Sysex deploy via MIDI. Requires NT firmware v1.13+ for the plug-in rescan
# sysex (no reboot needed). Tools: mido + python-rtmidi (installed via
# requirements.txt). NT must be connected over USB-MIDI and not held open
# by another app.
SYSEX_ID ?= 0
SYSEX_PLUGIN ?= build/arm/Hemispheres.o
deploy-sysex: $(SYSEX_PLUGIN)
	python3 harness/scripts/push_plugin_to_device.py $(SYSEX_ID) $(SYSEX_PLUGIN)

test: host test-applets
	python3 harness/scripts/run_scenario.py tests/scenarios/gainCustomUI/zero_signal.yaml

vendor:
	git submodule update --init --recursive

clean:
	rm -rf build/
