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
	@echo "make host     - build host simulator at build/host/sim_gainCustomUI"
	@echo "make test     - run all scripted scenarios"
	@echo "make deploy   - copy build/arm/*.o to DEVICE/programs/plug-ins/ (default DEVICE: /Volumes/NT; NT must be in USB disk mode)"
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

build/arm/screen_dump.o: applets/screen_dump.cpp shim/include/hem_dump_helper.h
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -Ishim/include -c -o $@ $<

build/arm/font_dump.o: applets/font_dump.cpp shim/include/hem_dump_helper.h
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -Ishim/include -c -o $@ $<

arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/screen_dump.o build/arm/font_dump.o

DEVICE ?= /Volumes/NT
PLUGIN_DIR := programs/plug-ins
deploy: arm
	@test -d "$(DEVICE)" || { echo "DEVICE=$(DEVICE) not mounted. Put the NT in USB disk mode via Misc menu first."; exit 1; }
	@mkdir -p "$(DEVICE)/$(PLUGIN_DIR)"
	cp build/arm/*.o "$(DEVICE)/$(PLUGIN_DIR)/"
	@echo "Deployed to $(DEVICE)/$(PLUGIN_DIR)/. Eject the volume, then press both encoders together on the NT to reboot into normal mode."

test: host
	python3 harness/scripts/run_scenario.py tests/scenarios/gainCustomUI/zero_signal.yaml

vendor:
	git submodule update --init --recursive

clean:
	rm -rf build/
