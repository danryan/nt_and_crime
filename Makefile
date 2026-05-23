# nt_and_crime top-level Makefile
.PHONY: help vendor host arm test test-runtime clean deploy deploy-sysex

ARM_CXX  := arm-none-eabi-c++
HOST_CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo g++)

NT_API_INCLUDE := vendor/distingNT_API/include
HEM_SRC_DIR    := vendor/O_C-Phazerville/software/src

ARM_FLAGS := -std=c++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
             -mthumb -fno-rtti -fno-exceptions -fno-threadsafe-statics \
             -fno-unwind-tables -fno-asynchronous-unwind-tables \
             -ffunction-sections -fdata-sections \
             -fvisibility=hidden -fvisibility-inlines-hidden \
             -fmerge-all-constants \
             -Os -fPIC -Wall \
             -I$(NT_API_INCLUDE)

ARM_FLAGS_VISIBLE_ENTRY := $(filter-out -fvisibility=hidden -fvisibility-inlines-hidden,$(ARM_FLAGS))

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

# Q1 host-set clip rect tests. Driven via a HemisphereApplet probe subclass
# in the test file; the shim's gfx wrappers clamp emits to a screen-space
# rect bounded by HS::gfx_clip_w / HS::gfx_clip_h.
build/host/test_draw_clip: harness/tests/test_draw_clip.cpp shim/src/globals.cpp shim/src/graphics.cpp shim/src/icons.cpp shim/src/quant/braids_quantizer.cpp shim/src/quant/OC_scales.cpp shim/src/quant/q_engine.cpp shim/src/cv_map/bjorklund.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-draw-clip
test-draw-clip: build/host/test_draw_clip
	./build/host/test_draw_clip

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

build/host/test_host_proxy: harness/tests/test_host_proxy.cpp shim/src/host_proxy.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) -o $@ $^

.PHONY: test-host-proxy
test-host-proxy: build/host/test_host_proxy
	./build/host/test_host_proxy

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

# Vendor example plug-ins: declare pluginEntry without our
# visibility-default attribute, so build them with default visibility.
build/arm/gainCustomUI.o: vendor/distingNT_API/examples/gainCustomUI.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS_VISIBLE_ENTRY) -c -o $@ $<

build/arm/gain.o: vendor/distingNT_API/examples/gain.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS_VISIBLE_ENTRY) -c -o $@ $<

build/arm/bus_probe.o: plugins/probes/bus_probe.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<

build/arm/reentrancy_probe.o: plugins/probes/reentrancy_probe.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o $@ $<

build/arm/section_probe.o: plugins/probes/section_probe.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o build/arm/section_probe.raw.o $<
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/section_probe.raw.o $@

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

build/arm/aeabi_probe.o: plugins/probes/aeabi_probe.cpp $(COMPILER_RT_OBJS)
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
# Per-variant vendor dep object lists. Variant 1 needs Lorenz (LowerRenz
# applet). Variant 2 has none of the Lorenz-dependent applets so its dep
# list is empty. If a future applet in variant 2 needs Lorenz, add the
# objects back to VARIANT2_DEP_OBJS.
VARIANT1_DEP_OBJS := $(VENDOR_DEP_ARM_OBJS)
VARIANT2_DEP_OBJS :=

define BUILD_ARM_HEMI_VARIANT
build/arm/$(1).o: applets/$(1).cpp $$(SHIM_DEPS) $$(COMPILER_RT_OBJS) $(3)
	mkdir -p build/arm
	$$(ARM_CXX) $$(ARM_FLAGS) -DHEMI_VARIANT=$(2) $$(SHIM_INCLUDE) $$(HEM_APPLET_INCLUDE) -c -o build/arm/$(1).raw.o $$<
	$$(ARM_LD) -r --strip-debug build/arm/$(1).raw.o $(3) $$(COMPILER_RT_OBJS) -o build/arm/$(1).merge1.o
	arm-none-eabi-objcopy --remove-section='.group' build/arm/$(1).merge1.o build/arm/$(1).nogroup.o
	$$(ARM_LD) -r -T shim/merge_sections.lds build/arm/$(1).nogroup.o -o build/arm/$(1).linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/$(1).linked.o $$@
endef

$(eval $(call BUILD_ARM_HEMI_VARIANT,Hemispheres,1,$(VARIANT1_DEP_OBJS)))
$(eval $(call BUILD_ARM_HEMI_VARIANT,Hemispheres2,2,$(VARIANT2_DEP_OBJS)))

# ---------------------------------------------------------------------------
# Per-applet plug-ins (mass-port release).
#
# Mass-port ships one .o per applet (build/arm/<APPLET>.o). Each per-applet
# .cpp lives at plugins/applets/<APPLET>.cpp and includes the manifest at
# shim/include/applet_manifests/<APPLET>.h plus the per-applet runtime header
# plugins/applets/_per_applet_runtime.h. Vendor dep linkage is per-applet via
# VENDOR_DEPS_<APPLET>; only applets needing vendor .cpp linkage (LowerRenz)
# set a non-empty list. All others compile their deps as headers into the
# per-applet TU.
# ---------------------------------------------------------------------------

# 6 pilots + 49 mass-port = 55 total. Order: pilots first, then batches.
ALL_APPLET_LIST := \
  Compare ClockDivider VectorLFO Cumulus Relabi ProbabilityDivider \
  AttenuateOffset Binary Button Logic Switch \
  Brancher Burst Calculate EnvFollow GameOfLife GateDelay GatedVCA \
  RndWalk Schmitt ShiftGate Slew Stairs TLNeuron Trending Voltage \
  VectorEG VectorMod VectorMorph \
  DualQuant OffsetQuant MultiScale ScaleDuet EnsOscKey Calibr8 Carpeggio \
  Chordinator EnigmaJr Pigeons Squanch Shredder Strum \
  Metronome ResetClock Shuffle Xfader Scope ClkToGate ClockSkip PolyDiv \
  ADEG ADSREG RunglBook LowerRenz Combin8

# Backwards-compat alias; existing rules still reference PILOT_APPLET_LIST.
PILOT_APPLET_LIST := $(ALL_APPLET_LIST)

VENDOR_DEPS_Compare            :=
VENDOR_DEPS_ClockDivider       :=
VENDOR_DEPS_VectorLFO          :=
VENDOR_DEPS_Cumulus            :=
VENDOR_DEPS_Relabi             :=
VENDOR_DEPS_ProbabilityDivider :=
VENDOR_DEPS_AttenuateOffset    :=
VENDOR_DEPS_Binary             :=
VENDOR_DEPS_Button             :=
VENDOR_DEPS_Logic              :=
VENDOR_DEPS_Switch             :=
VENDOR_DEPS_Brancher           :=
VENDOR_DEPS_Burst              :=
VENDOR_DEPS_Calculate          :=
VENDOR_DEPS_EnvFollow          :=
VENDOR_DEPS_GameOfLife         :=
VENDOR_DEPS_GateDelay          :=
VENDOR_DEPS_GatedVCA           :=
VENDOR_DEPS_RndWalk            :=
VENDOR_DEPS_Schmitt            :=
VENDOR_DEPS_ShiftGate          :=
VENDOR_DEPS_Slew               :=
VENDOR_DEPS_Stairs             :=
VENDOR_DEPS_TLNeuron           :=
VENDOR_DEPS_Trending           :=
VENDOR_DEPS_Voltage            :=
VENDOR_DEPS_VectorEG           :=
VENDOR_DEPS_VectorMod          :=
VENDOR_DEPS_VectorMorph        :=
VENDOR_DEPS_DualQuant          :=
VENDOR_DEPS_OffsetQuant        :=
VENDOR_DEPS_MultiScale         :=
VENDOR_DEPS_ScaleDuet          :=
VENDOR_DEPS_EnsOscKey          :=
VENDOR_DEPS_Calibr8            :=
VENDOR_DEPS_Carpeggio          :=
VENDOR_DEPS_Chordinator        :=
VENDOR_DEPS_EnigmaJr           :=
VENDOR_DEPS_Pigeons            :=
VENDOR_DEPS_Squanch            :=
VENDOR_DEPS_Shredder           :=
VENDOR_DEPS_Strum              :=
VENDOR_DEPS_Metronome          :=
VENDOR_DEPS_ResetClock         :=
VENDOR_DEPS_Shuffle            :=
VENDOR_DEPS_Xfader             :=
VENDOR_DEPS_Scope              :=
VENDOR_DEPS_ClkToGate          :=
VENDOR_DEPS_ClockSkip          :=
VENDOR_DEPS_PolyDiv            :=
VENDOR_DEPS_ADEG               :=
VENDOR_DEPS_ADSREG             :=
VENDOR_DEPS_RunglBook          :=
VENDOR_DEPS_LowerRenz          := build/arm/shim_src/lorenz/streams_resources.o build/arm/shim_src/lorenz/streams_lorenz_generator.o
VENDOR_DEPS_Combin8            :=

# $(1) = applet name (e.g. Compare). $(2) = expanded VENDOR_DEPS_<applet>.
define BUILD_PER_APPLET
build/arm/$(1).o: plugins/applets/$(1).cpp shim/include/applet_manifests/$(1).h plugins/applets/_per_applet_runtime.h $$(SHIM_DEPS) $$(COMPILER_RT_OBJS) $(2)
	mkdir -p build/arm
	$$(ARM_CXX) $$(ARM_FLAGS) $$(SHIM_INCLUDE) $$(HEM_APPLET_INCLUDE) -c -o build/arm/$(1).raw.o $$<
	$$(ARM_LD) -r --strip-debug build/arm/$(1).raw.o $(2) $$(COMPILER_RT_OBJS) -o build/arm/$(1).merge1.o
	arm-none-eabi-objcopy --remove-section='.group' build/arm/$(1).merge1.o build/arm/$(1).nogroup.o
	$$(ARM_LD) -r -T shim/merge_sections.lds build/arm/$(1).nogroup.o -o build/arm/$(1).linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/$(1).linked.o $$@
endef

# Only generate build rules for applets whose plugins/applets/<APPLET>.cpp
# exists on disk. During mass-port fan-out, implementer worktrees create
# the .cpp on their branch; the parent worktree generates rules dynamically
# as the .cpp files land via cherry-picks.
PRESENT_APPLETS := $(filter-out ,$(foreach a,$(ALL_APPLET_LIST),$(if $(wildcard plugins/applets/$(a).cpp),$(a),)))

$(foreach a,$(PRESENT_APPLETS),$(eval $(call BUILD_PER_APPLET,$(a),$(VENDOR_DEPS_$(a)))))

PILOT_APPLET_OBJS := $(addprefix build/arm/, $(addsuffix .o, $(PRESENT_APPLETS)))
ALL_APPLET_OBJS   := $(PILOT_APPLET_OBJS)

# ---------------------------------------------------------------------------
# Host plug-ins (Hemispheres host, Quadrants host).
#
# Each host source includes shim/include/host_helpers.h and links against
# shim/src/host_helpers.cpp. Hosts do not include per-applet runtime; they
# call through HemiPluginInterface function pointers populated by each
# per-applet plug-in's construct().
# ---------------------------------------------------------------------------

HOST_PLUGIN_LIST := Hemispheres_host Quadrants_host

build/arm/shim_src/host_helpers.o: shim/src/host_helpers.cpp shim/include/host_helpers.h shim/include/HemiPluginInterface.h
	mkdir -p $(@D)
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) -c -o $@ $<

build/arm/shim_src/host_proxy.o: shim/src/host_proxy.cpp shim/include/host_proxy.h
	mkdir -p $(@D)
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) -c -o $@ $<

# $(1) = host name (Hemispheres_host or Quadrants_host). Both hosts now
# link the host_proxy aggregator unconditionally.
define BUILD_HOST_PLUGIN
build/arm/$(1).o: plugins/hosts/$(1).cpp build/arm/shim_src/host_helpers.o build/arm/shim_src/host_proxy.o $$(SHIM_DEPS) $$(COMPILER_RT_OBJS)
	mkdir -p build/arm
	$$(ARM_CXX) $$(ARM_FLAGS) $$(SHIM_INCLUDE) -c -o build/arm/$(1).raw.o $$<
	$$(ARM_LD) -r --strip-debug build/arm/$(1).raw.o build/arm/shim_src/host_helpers.o build/arm/shim_src/host_proxy.o $$(COMPILER_RT_OBJS) -o build/arm/$(1).merge1.o
	arm-none-eabi-objcopy --remove-section='.group' build/arm/$(1).merge1.o build/arm/$(1).nogroup.o
	$$(ARM_LD) -r -T shim/merge_sections.lds build/arm/$(1).nogroup.o -o build/arm/$(1).linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/$(1).linked.o $$@
endef

$(foreach h,$(HOST_PLUGIN_LIST),$(eval $(call BUILD_HOST_PLUGIN,$(h))))

HOST_PLUGIN_OBJS := $(addprefix build/arm/, $(addsuffix .o, $(HOST_PLUGIN_LIST)))

# Vendor dep cpp sources linked into the host test binary. Per-dep Catch2
# binaries #include these .cpp files directly inline; for the applet host
# build the same code is compiled as separate TUs and linked in.
VENDOR_DEP_HOST_SRCS := shim/src/lorenz/streams_resources.cpp \
                       shim/src/lorenz/streams_lorenz_generator.cpp

# `make test-applets` is preserved as an alias for the per-applet test
# runner. The bundled-host test binary that this target originally drove
# was retired in the cleanup release.
.PHONY: test-applets
test-applets: test-applets-pilot

# Per-applet host test binaries (pilot release). Each binary loads the
# matching plugins/applets/<APPLET>.cpp into the harness via plugin_loader
# and runs the Catch2 cases in harness/tests/test_applet_<APPLET>.cpp.
build/host/test_applet_%: harness/tests/test_applet_%.cpp plugins/applets/%.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS) $(VENDOR_DEP_HOST_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-applets-pilot
test-applets-pilot: $(addprefix build/host/test_applet_, $(PILOT_APPLET_LIST))
	@for t in $^; do echo "Running $$t"; ./$$t || exit 1; done

# Per-host test binaries. Each binary loads the matching
# plugins/hosts/<HOST>.cpp into the harness and runs the Catch2 cases in
# harness/tests/test_host_<HOST>.cpp. Host tests install fake
# HemiPluginInterface stubs in slot positions and verify routing.
#
# Explicit rule for the Hemispheres host proxy test binary: links
# shim/src/host_proxy.cpp in addition to host_helpers so the host's
# proxy aggregator wiring can be exercised end-to-end. Explicit rules
# beat the test_host_% pattern below.
build/host/test_host_Hemispheres_host_proxy: harness/tests/test_host_Hemispheres_host_proxy.cpp plugins/hosts/Hemispheres_host.cpp shim/src/host_helpers.cpp shim/src/host_proxy.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) -o $@ $^

.PHONY: test-host-Hemispheres-proxy
test-host-Hemispheres-proxy: build/host/test_host_Hemispheres_host_proxy
	./build/host/test_host_Hemispheres_host_proxy

# The test_host_% pattern links host_proxy.cpp because all hosts now
# reference host_proxy:: after the stage-3 wiring.
build/host/test_host_%: harness/tests/test_host_%.cpp plugins/hosts/%.cpp shim/src/host_helpers.cpp shim/src/host_proxy.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) -o $@ $^

# Proxy-aggregator host test (companion to test_host_Quadrants_host).
# Drives Quadrants_host.cpp through the host_proxy injection seam to
# verify parameterChanged forwarding and construct-time guard.
build/host/test_host_Quadrants_host_proxy: harness/tests/test_host_Quadrants_host_proxy.cpp plugins/hosts/Quadrants_host.cpp shim/src/host_helpers.cpp shim/src/host_proxy.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) -o $@ $^

.PHONY: test-host-Quadrants-host-proxy
test-host-Quadrants-host-proxy: build/host/test_host_Quadrants_host_proxy
	./build/host/test_host_Quadrants_host_proxy

.PHONY: test-hosts-pilot
test-hosts-pilot: $(addprefix build/host/test_host_, $(HOST_PLUGIN_LIST))
	@for t in $^; do echo "Running $$t"; ./$$t || exit 1; done

# Per-dep tests. Each Catch2 binary builds against the same harness shim
# as the per-applet tests but is standalone so a single dep test can run
# in isolation. test-deps runs all 6.
DEP_TESTS := test_dep_vec_osc test_dep_lorenz test_dep_tideslite \
             test_dep_clock_mgr test_dep_quant test_dep_cv_map

# Shim core sources (globals, graphics, icons, cxx runtime stubs). Linked
# into dep tests so dep tests do not pull vendor copies of vec_osc /
# lorenz dep headers (which would collide with the shim copies the dep
# tests already include).
SHIM_CORE_SRCS := shim/src/globals.cpp shim/src/graphics.cpp shim/src/icons.cpp \
                  shim/src/quant/braids_quantizer.cpp shim/src/quant/OC_scales.cpp \
                  shim/src/quant/q_engine.cpp shim/src/cv_map/bjorklund.cpp

build/host/test_dep_%: harness/tests/test_dep_%.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS) $(VENDOR_DEP_HOST_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-deps
test-deps: $(addprefix build/host/, $(DEP_TESTS))
	@for t in $^; do echo "Running $$t"; ./$$t || exit 1; done

arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/aeabi_probe.o build/arm/reentrancy_probe.o build/arm/Hemispheres.o build/arm/Hemispheres2.o $(PILOT_APPLET_OBJS) $(HOST_PLUGIN_OBJS)

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
