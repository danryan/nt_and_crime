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
	@echo "make test-applets - run per-applet host Catch2 binaries (alias for test-applets-pilot)"
	@echo "make deploy        - copy build/arm/*.o to DEVICE/programs/plug-ins/ (default DEVICE: /Volumes/NT; NT must be in USB disk mode)"
	@echo "make deploy-sysex  - push build/arm/Hemispheres_host.o via USB-MIDI sysex (NT firmware v1.13+, no reboot)"
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

build/host/test_oc_apps: harness/tests/test_oc_apps.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) -o $@ $^

.PHONY: test-oc-apps
test-oc-apps: build/host/test_oc_apps
	./build/host/test_oc_apps

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
# -Ishim/include MUST stay before -I$(HEM_SRC_DIR): shim stubs shadow same-named vendor headers.
SHIM_INCLUDE := -Ishim/include -I$(HEM_SRC_DIR)
HEM_APPLET_INCLUDE := -Ivendor/O_C-Phazerville/software/src/applets

SHIM_DEPS := $(wildcard shim/include/*.h) $(wildcard shim/include/*/*.h) $(wildcard shim/src/*.cpp)

# compiler-rt builtins compiled with -fPIC to match plug-in PIC code model.
# Toolchain ships no PIC libgcc multilib for v7e-m+dp/hard, so the build
# sources compiler-rt directly from the vendor/llvm-project submodule
# (sparse-checkout of compiler-rt/lib/builtins/, pinned at llvmorg-19.1.0).
ARM_LD := arm-none-eabi-ld
ARM_CC := arm-none-eabi-gcc
# compiler-rt files compiled as C (not C++) so symbols are not name-mangled.
# Strip the C++-only ARM_FLAGS entries (-fno-rtti, -fno-exceptions,
# -fno-threadsafe-statics) when compiling .c files; gcc accepts the C++ flags
# silently but it is cleaner to express intent.
ARM_CFLAGS := -std=c99 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
              -mthumb -Os -fPIC -Wall -I$(NT_API_INCLUDE)

COMPILER_RT_DIR := vendor/llvm-project/compiler-rt/lib/builtins
COMPILER_RT_SRCS := \
    $(COMPILER_RT_DIR)/divdi3.c \
    $(COMPILER_RT_DIR)/udivdi3.c \
    $(COMPILER_RT_DIR)/divmoddi4.c \
    $(COMPILER_RT_DIR)/udivmoddi4.c \
    $(COMPILER_RT_DIR)/fixdfdi.c \
    $(COMPILER_RT_DIR)/fixunsdfdi.c \
    $(COMPILER_RT_DIR)/arm/aeabi_div0.c \
    $(COMPILER_RT_DIR)/arm/aeabi_ldivmod.S \
    $(COMPILER_RT_DIR)/arm/aeabi_uldivmod.S
COMPILER_RT_OBJS := \
    $(patsubst $(COMPILER_RT_DIR)/%.c,build/arm/compiler_rt/%.o,$(filter %.c,$(COMPILER_RT_SRCS))) \
    $(patsubst $(COMPILER_RT_DIR)/%.S,build/arm/compiler_rt/%.o,$(filter %.S,$(COMPILER_RT_SRCS)))

build/arm/compiler_rt/%.o: $(COMPILER_RT_DIR)/%.c
	mkdir -p $(@D)
	$(ARM_CC) $(ARM_CFLAGS) -I$(COMPILER_RT_DIR) -c -o $@ $<

build/arm/aeabi_probe.o: plugins/probes/aeabi_probe.cpp $(COMPILER_RT_OBJS)
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o build/arm/aeabi_probe.raw.o $<
	$(ARM_LD) -r --strip-debug build/arm/aeabi_probe.raw.o $(COMPILER_RT_OBJS) -o build/arm/aeabi_probe.linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/aeabi_probe.linked.o $@

build/arm/compiler_rt/%.o: $(COMPILER_RT_DIR)/%.S
	mkdir -p $(@D)
	$(ARM_CC) $(ARM_CFLAGS) -I$(COMPILER_RT_DIR) -c -o $@ $<

# Pattern rule for shim/src/ .cpp implementations (host_helpers, host_proxy,
# and any other shim-owned source). Vendor dep cpps are compiled in place via
# the build/arm/vendor_src/%.o rule below.
build/arm/shim_src/%.o: shim/src/%.cpp
	mkdir -p $(@D)
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) -c -o $@ $<

# Pattern rule for vendor dep .cpp implementations compiled in place from
# the vendor source tree. The vendor cpps include their headers by bare name;
# compiling in place lets the compiler find the sibling header via its
# own-directory search, so no forwarding bridge is needed.
build/arm/vendor_src/%.o: $(HEM_SRC_DIR)/%.cpp
	mkdir -p $(@D)
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) -c -o $@ $<

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
VENDOR_DEPS_LowerRenz          := build/arm/vendor_src/streams_resources.o build/arm/vendor_src/streams_lorenz_generator.o
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
# Per-app O_C full-screen plug-ins.
#
# Mirrors the per-applet model exactly. Each O_C-app ships one .o per app
# (build/arm/<APP>.o). Each per-app .cpp lives at plugins/apps/<APP>.cpp,
# includes its manifest at shim/include/oc_app_manifests/<APP>.h and the
# per-app runtime plugins/apps/_per_app_runtime.h. The .cpp defines
# NT_OC_APP_TU at its top, which pulls the OC shim impl aggregation
# (shim/include/oc_shim_impl.h) into the single per-app TU -- the same single-
# aggregating-TU model the applets use via hem_shim.h. The build also passes
# -DNT_OC_APP_TU on the ARM compile so the trigger is explicit at the rule
# level; the source-level define makes it idempotent.
#
# Per-app vendor .cpp linkage is via VENDOR_DEPS_<APP> (ARM-side, partial-
# linked .o files) and VENDOR_DEP_HOST_SRCS_<APP> (host-side, source files for
# the test binary). The stub needs neither.
# ---------------------------------------------------------------------------

OC_APP_LIST := StubApp Low_rents Harrington1200 FPART

VENDOR_DEPS_StubApp          :=
VENDOR_DEP_HOST_SRCS_StubApp :=

# FPART (APP_FPART) needs no net-new vendor .cpp: all headers it pulls
# (OC_apps, OC_menus, OC_strings, OC_DAC/ADC, OC_digital_inputs, util_settings,
# util_math) are header-only or shim-shadowed. Its scale/root tables are inline
# in the vendor header.
VENDOR_DEPS_FPART          :=
VENDOR_DEP_HOST_SRCS_FPART :=

# Low-rents (APP_LORENZ) links the streams Lorenz generator + its resource LUTs.
# ARM side: the already-built partial-link objects (same pair the LowerRenz
# applet uses). Host side: the vendor sources compiled into the test binary.
VENDOR_DEPS_Low_rents          := build/arm/vendor_src/streams_resources.o build/arm/vendor_src/streams_lorenz_generator.o
VENDOR_DEP_HOST_SRCS_Low_rents := $(HEM_SRC_DIR)/streams_resources.cpp $(HEM_SRC_DIR)/streams_lorenz_generator.cpp

# Harrington 1200 (APP_H1200) needs no net-new vendor .cpp. The string tables
# and the trigger-delay ticks array it reads (note_names, cv_input_names_none,
# trigger_delay_times, trigger_delay_ticks) are all shim-owned in
# shim/src/globals.cpp, which rides the OC shim-impl aggregation. Linking vendor
# OC_strings.cpp would duplicate note_names/note_names_unpadded/capital_letters
# (also in globals.cpp) and fail the link on both host and ARM. Euclidean
# (bjorklund) and SemitoneQuantizer likewise ride the aggregation.
VENDOR_DEPS_Harrington1200          :=
VENDOR_DEP_HOST_SRCS_Harrington1200 :=

# $(1) = app name (e.g. StubApp). $(2) = expanded VENDOR_DEPS_<app>.
# Identical pipeline to BUILD_PER_APPLET: compile the per-app TU with
# -DNT_OC_APP_TU so it aggregates the OC shim impl, partial-link the vendor
# deps + compiler-rt builtins, strip the COMDAT group, merge the per-method
# COMDAT sections via merge_sections.lds, then strip the unwanted metadata
# sections.
define BUILD_PER_OC_APP
build/arm/$(1).o: plugins/apps/$(1).cpp shim/include/oc_app_manifests/$(1).h plugins/apps/_per_app_runtime.h $$(SHIM_DEPS) $$(COMPILER_RT_OBJS) $(2)
	mkdir -p build/arm
	$$(ARM_CXX) $$(ARM_FLAGS) $$(SHIM_INCLUDE) $$(HEM_APPLET_INCLUDE) -DNT_OC_APP_TU -c -o build/arm/$(1).raw.o $$<
	$$(ARM_LD) -r --strip-debug build/arm/$(1).raw.o $(2) $$(COMPILER_RT_OBJS) -o build/arm/$(1).merge1.o
	arm-none-eabi-objcopy --remove-section='.group' build/arm/$(1).merge1.o build/arm/$(1).nogroup.o
	$$(ARM_LD) -r -T shim/merge_sections.lds build/arm/$(1).nogroup.o -o build/arm/$(1).linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/$(1).linked.o $$@
endef

# Generate ARM build rules only for apps whose plugins/apps/<APP>.cpp exists,
# matching the per-applet PRESENT_APPLETS discipline.
PRESENT_OC_APPS := $(filter-out ,$(foreach a,$(OC_APP_LIST),$(if $(wildcard plugins/apps/$(a).cpp),$(a),)))

$(foreach a,$(PRESENT_OC_APPS),$(eval $(call BUILD_PER_OC_APP,$(a),$(VENDOR_DEPS_$(a)))))

OC_APP_OBJS := $(addprefix build/arm/, $(addsuffix .o, $(PRESENT_OC_APPS)))

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

# Vendor dep cpp sources compiled as separate TUs and linked into the host
# test binaries (both per-applet test_applet_% and per-dep test_dep_%). The
# test .cpp files include only the vendor headers; these sources supply the
# definitions so the vendor code links once without duplicate symbols.
VENDOR_DEP_HOST_SRCS := $(HEM_SRC_DIR)/streams_resources.cpp \
                       $(HEM_SRC_DIR)/streams_lorenz_generator.cpp

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

# Shim core source implementations (globals, graphics, icons, the quant
# engine, and cv_map) compiled as TUs and linked into the host test
# binaries, so the symbols those tests reference are defined exactly once.
SHIM_CORE_SRCS := shim/src/globals.cpp shim/src/graphics.cpp shim/src/icons.cpp \
                  shim/src/quant/braids_quantizer.cpp shim/src/quant/OC_scales.cpp \
                  shim/src/quant/q_engine.cpp shim/src/cv_map/bjorklund.cpp

build/host/test_dep_%: harness/tests/test_dep_%.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS) $(VENDOR_DEP_HOST_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-deps
test-deps: $(addprefix build/host/, $(DEP_TESTS))
	@for t in $^; do echo "Running $$t"; ./$$t || exit 1; done

# O_C apps foundation: shim DAC, ADC, digital-input accessor behavior.
# Standalone host test mirroring the test_dep_% rule. Links SHIM_CORE_SRCS so
# the extern DAC_CHANNEL_* channel objects (defined in shim/src/globals.cpp)
# resolve, plus shim/src/oc/io.cpp for the O_C-only ADC_CHANNEL_* channel
# objects and the ADC/digital-input backing state. io.cpp is O_C-only and is
# not part of SHIM_CORE_SRCS (which is aggregated into every Hemisphere applet).
build/host/test_oc_io: harness/tests/test_oc_io.cpp shim/src/oc/io.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS) $(VENDOR_DEP_HOST_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-oc-io
test-oc-io: build/host/test_oc_io
	./build/host/test_oc_io

# O_C menus foundation: the hand-ported menu widgets (ScreenCursor, SettingsList,
# SettingsListItem, TitleBar) plus vectorscope_render and visualize_pitch_classes.
# Mirrors the test_oc_io rule: links SHIM_CORE_SRCS (globals.cpp for the
# DAC_CHANNEL_* channel objects, graphics.cpp for the non-inline draw primitives)
# plus shim/src/oc/menus.cpp. menus.cpp is O_C-only and is NOT part of
# SHIM_CORE_SRCS (which aggregates into every Hemisphere applet).
build/host/test_oc_menus: harness/tests/test_oc_menus.cpp shim/src/oc/menus.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS) $(VENDOR_DEP_HOST_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-oc-menus
test-oc-menus: build/host/test_oc_menus
	./build/host/test_oc_menus

# O_C_strings test: vendor OC_strings.cpp defines the new string tables and delay
# ticks array. We link OC_strings.cpp directly and exclude the old string definitions
# from globals.cpp to avoid duplicate symbol errors. We still need HARNESS_SRCS for
# Catch2 main but not SHIM_CORE_SRCS which contains globals.cpp.
build/host/test_oc_strings: harness/tests/test_oc_strings.cpp $(HEM_SRC_DIR)/OC_strings.cpp $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-oc-strings
test-oc-strings: build/host/test_oc_strings
	./build/host/test_oc_strings

# O_C apps runtime test: exercises the per-app runtime helpers in
# plugins/apps/_per_app_runtime.h (cadence accumulator, one-edge-per-tick,
# centering shift, sentinel, enum-offset, settings round-trip). Links the
# OC-only I/O backing (shim/src/oc/io.cpp) so the runtime can refresh
# OC::ADC and OC::DigitalInputs from the test-injected backing state.
build/host/test_oc_runtime: harness/tests/test_oc_runtime.cpp shim/src/oc/io.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-oc-runtime
test-oc-runtime: build/host/test_oc_runtime
	./build/host/test_oc_runtime

# O_C apps router test: foundation-level control-router coverage. Uses the
# reusable _NT_uiData synthesis helper (harness/include/oc_ui_sim.h) to drive
# the per-app runtime's customUi and asserts on the router primitives
# (classify_release, was_long_press_already_emitted, last_controls_of, the
# mapping table). Links the same OC-only I/O backing as test_oc_runtime.
build/host/test_oc_router: harness/tests/test_oc_router.cpp shim/src/oc/io.cpp $(SHIM_CORE_SRCS) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-oc-router
test-oc-router: build/host/test_oc_router
	./build/host/test_oc_router

# Per-app O_C host test binaries. Each binary links the per-app .cpp (which
# aggregates the OC shim impl into its single TU) plus the matching test .cpp
# and the harness. It deliberately does NOT add SHIM_CORE_SRCS or oc/io.cpp:
# the aggregating per-app TU already supplies every shim symbol, so linking the
# shim sources again would produce duplicate definitions. It also does NOT pass
# -DNT_OC_APP_TU, so the TEST TU does not aggregate (only the .cpp does). A
# per-app VENDOR_DEP_HOST_SRCS_<APP> supplies any host-compiled vendor sources
# (e.g. OC_strings.cpp) a real app needs; the stub needs none.
# Secondary expansion lets the per-app VENDOR_DEP_HOST_SRCS_<APP> reach the
# prerequisite list: the $* stem is not bound during the first prerequisite
# expansion of a pattern rule, so $(VENDOR_DEP_HOST_SRCS_$*) would expand to
# empty without it. With .SECONDEXPANSION enabled the escaped $$* binds on the
# second pass. The rules that follow only reference already-resolved file lists
# in their prerequisites, so re-expanding them is idempotent.
.SECONDEXPANSION:
build/host/test_oc_app_%: harness/tests/test_oc_app_%.cpp plugins/apps/%.cpp $(HARNESS_SRCS) $$(VENDOR_DEP_HOST_SRCS_$$*)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

# Convenience target for a single app's host test (e.g. make test-oc-app-StubApp).
.PHONY: test-oc-app-%
test-oc-app-%: build/host/test_oc_app_%
	./build/host/test_oc_app_$*

# Run every present O_C-app host test.
.PHONY: test-oc-apps-all
test-oc-apps-all: $(addprefix build/host/test_oc_app_, $(PRESENT_OC_APPS))
	@for t in $^; do echo "Running $$t"; ./$$t || exit 1; done

arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/aeabi_probe.o build/arm/reentrancy_probe.o $(PILOT_APPLET_OBJS) $(HOST_PLUGIN_OBJS) $(OC_APP_OBJS)

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
SYSEX_PLUGIN ?= build/arm/Hemispheres_host.o
deploy-sysex: $(SYSEX_PLUGIN)
	python3 harness/scripts/push_plugin_to_device.py $(SYSEX_ID) $(SYSEX_PLUGIN)

test: host test-applets
	python3 harness/scripts/run_scenario.py tests/scenarios/gainCustomUI/zero_signal.yaml

vendor:
	./bootstrap.sh

clean:
	rm -rf build/
