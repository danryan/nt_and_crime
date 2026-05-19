# nt_and_crime top-level Makefile
.PHONY: help vendor host arm test test-runtime clean deploy deploy-sysex

ARM_CXX  := arm-none-eabi-c++
HOST_CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo g++)

NT_API_INCLUDE := vendor/distingNT_API/include
HEM_SRC_DIR    := vendor/O_C-Phazerville/software/src

ARM_FLAGS := -std=c++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
             -mthumb -fno-rtti -fno-exceptions -fno-threadsafe-statics \
             -Os -fPIC -Wall \
             -I$(NT_API_INCLUDE)

HOST_FLAGS := -std=c++17 -fno-rtti -fno-exceptions -Wall -O2 \
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

# aeabi_probe rule lives below COMPILER_RT_OBJS so its $(COMPILER_RT_OBJS)
# prereq expansion is non-empty (Make expands prereqs at rule-parse time).

# Hem shim sources (header-only for now; compiled as part of each applet's TU)
SHIM_INCLUDE := -Ishim/include
HEM_APPLET_INCLUDE := -Ivendor/O_C-Phazerville/software/src/applets

SHIM_DEPS := $(wildcard shim/include/*.h) $(wildcard shim/include/*/*.h) $(wildcard shim/src/*.cpp)

# compiler-rt builtins compiled with -fPIC to match plug-in PIC code model.
# Toolchain ships no PIC libgcc multilib for v7e-m+dp/hard, so we vendor
# compiler-rt sources directly. Sources under shim/src/compiler_rt/ are
# verbatim from llvm-project tag llvmorg-19.1.0.
ARM_LD := arm-none-eabi-ld
ARM_CC := arm-none-eabi-gcc
# compiler-rt files compiled as C (not C++) so symbols are not name-mangled.
# Strip the C++-only ARM_FLAGS entries (-fno-rtti, -fno-exceptions,
# -fno-threadsafe-statics) when compiling .c files; gcc accepts the C++ flags
# silently but it is cleaner to express intent.
ARM_CFLAGS := -std=c99 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
              -mthumb -Os -fPIC -Wall -I$(NT_API_INCLUDE)

COMPILER_RT_SRCS := \
    shim/src/compiler_rt/divdi3.c \
    shim/src/compiler_rt/udivdi3.c \
    shim/src/compiler_rt/divmoddi4.c \
    shim/src/compiler_rt/udivmoddi4.c \
    shim/src/compiler_rt/fixdfdi.c \
    shim/src/compiler_rt/fixunsdfdi.c \
    shim/src/compiler_rt/arm/aeabi_div0.c \
    shim/src/compiler_rt/arm/aeabi_ldivmod.S \
    shim/src/compiler_rt/arm/aeabi_uldivmod.S
COMPILER_RT_OBJS := \
    $(patsubst shim/src/compiler_rt/%.c,build/arm/compiler_rt/%.o,$(filter %.c,$(COMPILER_RT_SRCS))) \
    $(patsubst shim/src/compiler_rt/%.S,build/arm/compiler_rt/%.o,$(filter %.S,$(COMPILER_RT_SRCS)))

build/arm/compiler_rt/%.o: shim/src/compiler_rt/%.c
	mkdir -p $(@D)
	$(ARM_CC) $(ARM_CFLAGS) -Ishim/src/compiler_rt -c -o $@ $<

build/arm/aeabi_probe.o: applets/aeabi_probe.cpp $(COMPILER_RT_OBJS)
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o build/arm/aeabi_probe.raw.o $<
	$(ARM_LD) -r --strip-debug build/arm/aeabi_probe.raw.o $(COMPILER_RT_OBJS) -o build/arm/aeabi_probe.linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/aeabi_probe.linked.o $@

build/arm/compiler_rt/%.o: shim/src/compiler_rt/%.S
	mkdir -p $(@D)
	$(ARM_CC) $(ARM_CFLAGS) -Ishim/src/compiler_rt -c -o $@ $<

# Vendor dep sources that ship .cpp implementations (not header-only). LowerRenz
# references streams::LorenzGenerator::Init/Process; the firmware does not provide
# them, so they must be linked into Hemispheres.o. streams_resources.cpp carries the
# constant tables LorenzGenerator references at runtime.
VENDOR_DEP_ARM_SRCS := shim/src/lorenz/streams_resources.cpp \
                      shim/src/lorenz/streams_lorenz_generator.cpp
VENDOR_DEP_ARM_OBJS := $(patsubst shim/src/%.cpp,build/arm/shim_src/%.o,$(VENDOR_DEP_ARM_SRCS))

build/arm/shim_src/%.o: shim/src/%.cpp
	mkdir -p $(@D)
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) -c -o $@ $<

# Hemispheres ARM build pipeline. The on-device applet set is split across
# two .o files: Hemispheres.o (primary, variant 1) holds 51 applets, and
# Hemispheres2.o (secondary, variant 2) holds the 5 largest applets that
# would otherwise push the primary past the NT firmware's per-plug-in .text
# budget (Relabi, Shredder, EnsOscKey, VectorLFO, Strum). Each .o must fit
# under the budget (empirically ~82KB) independently.
#
# HEMI_VARIANT selects the applet subset HemispheresFactory.h registers:
#   0 = host build (all 56 applets; tests need them)
#   1 = ARM primary
#   2 = ARM secondary
#
# Section pipeline: compile -> partial-link with lorenz + compiler-rt ->
# strip COMDAT groups -> re-partial-link with merge_sections.lds (collapses
# split COMDAT subsections into single .text/.data/.rodata, reducing
# section count from ~1640 to ~14). Required because gcc emits
# `.text.<mangled>` per C++ COMDAT inline method; NT firmware fails to
# load plug-ins past ~1600 sections.
define BUILD_ARM_HEMI_VARIANT
build/arm/$(1).o: applets/$(1).cpp $$(SHIM_DEPS) $$(COMPILER_RT_OBJS) $$(VENDOR_DEP_ARM_OBJS)
	mkdir -p build/arm
	$$(ARM_CXX) $$(ARM_FLAGS) -DHEMI_VARIANT=$(2) $$(SHIM_INCLUDE) $$(HEM_APPLET_INCLUDE) -c -o build/arm/$(1).raw.o $$<
	$$(ARM_LD) -r --strip-debug build/arm/$(1).raw.o $$(VENDOR_DEP_ARM_OBJS) $$(COMPILER_RT_OBJS) -o build/arm/$(1).merge1.o
	arm-none-eabi-objcopy --remove-section='.group' build/arm/$(1).merge1.o build/arm/$(1).nogroup.o
	$$(ARM_LD) -r -T shim/merge_sections.lds build/arm/$(1).nogroup.o -o build/arm/$(1).linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/$(1).linked.o $$@
endef

$(eval $(call BUILD_ARM_HEMI_VARIANT,Hemispheres,1))
$(eval $(call BUILD_ARM_HEMI_VARIANT,Hemispheres2,2))

build/host/Hemispheres.host.o: applets/Hemispheres.cpp $(SHIM_DEPS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<

# Vendor dep cpp sources linked into the host test binary. Per-dep Catch2
# binaries #include these .cpp files directly inline; for the applet host
# build the same code is compiled as separate TUs and linked in.
VENDOR_DEP_HOST_SRCS := shim/src/lorenz/streams_resources.cpp \
                       shim/src/lorenz/streams_lorenz_generator.cpp

build/host/test_hemispheres: harness/tests/test_hemispheres.cpp harness/tests/applet_test_helpers.cpp build/host/Hemispheres.host.o $(HARNESS_SRCS) $(VENDOR_DEP_HOST_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-applets
test-applets: build/host/test_hemispheres
	./build/host/test_hemispheres

# Per-dep tests. Each Catch2 binary builds against the same harness shim
# as test_hemispheres but is standalone so a single dep test can run in
# isolation. test-deps runs all 6.
DEP_TESTS := test_dep_vec_osc test_dep_lorenz test_dep_tideslite \
             test_dep_clock_mgr test_dep_quant test_dep_cv_map

# Shim core sources (globals, graphics, icons, cxx runtime stubs). Linked
# into dep tests in place of Hemispheres.host.o so dep tests do not pull
# vendor copies of vec_osc / lorenz dep headers (which would collide with
# the shim copies the dep tests already include).
SHIM_CORE_SRCS := shim/src/globals.cpp shim/src/graphics.cpp shim/src/icons.cpp \
                  shim/src/quant/braids_quantizer.cpp shim/src/quant/OC_scales.cpp \
                  shim/src/quant/q_engine.cpp shim/src/cv_map/bjorklund.cpp

build/host/test_dep_%: harness/tests/test_dep_%.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS) $(VENDOR_DEP_HOST_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-deps
test-deps: $(addprefix build/host/, $(DEP_TESTS))
	@for t in $^; do echo "Running $$t"; ./$$t || exit 1; done

arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/aeabi_probe.o build/arm/Hemispheres.o build/arm/Hemispheres2.o

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
