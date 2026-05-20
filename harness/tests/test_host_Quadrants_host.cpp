// Host test: Quadrants Host plug-in.
//
// Test strategy
// -------------
// NT_getSlot is a stub in the harness that unconditionally returns false.
// Host tests bypass it through the NT_HEM_HOST_SIM injection API exported by
// Quadrants_host.cpp:
//   qq_test_inject_slot(slot_idx, plugin*, guid) -- pre-populate a slot
//   qq_test_clear_slots()                        -- reset before each test
//   qq_test_get_focused_slot(alg*)               -- read focused_slot_idx
//   qq_test_set_focused_slot(alg*, idx)          -- write focused_slot_idx
//
// _NT_slot member functions (guid(), plugin(), etc.) are firmware-side symbols
// never called in host tests because NT_getSlot returns false. No-op stubs are
// provided below to satisfy the linker.
//
// Note on build blocker (pre-existing setup bug):
// shim/src/host_helpers.cpp includes <distingnt/slot.h> after <distingnt/api.h>
// without a prior #include <cstddef>. slot.h uses NULL, which is not in scope
// on clang++ without <cstddef>. Quadrants_host.cpp works around this within its
// TU by including <cstddef> and <distingnt/slot.h> before host_helpers.h, using
// the include guard to prevent the failing re-include. host_helpers.cpp itself
// is an independent TU and cannot benefit from this ordering. The integration
// step must add #include <cstddef> to shim/include/host_helpers.h to fix that
// TU and allow the host test binary to link and run.
//
// Covered:
//   QH1a-d: button1-4 edges set focused slot to 0-3.
//   QH2: L encoder routes only to focused slot (on_encoder_turn).
//   QH3: R encoder routes only to focused slot (on_encoder_turn_shifted).
//   QH4: L encoder button edge routes to focused slot on_button_press.
//   QH5: R encoder button edge routes to focused slot on_aux_button.
//   QH6: ABI mismatch (magic=0) in focused slot: no routing, no crash.
//   QH7: Empty slot (qq_test_inject_slot with nullptr): no crash.
//   QH8: All slots empty: draw renders incompatible stubs, no crash.
//   QH9: serialise/deserialise round-trip preserves focused_slot_idx.
//   QH10: hasCustomUi returns correct bitmask.
//   QH11: Holding button1 (no rising edge) does not re-trigger focus change.
//   QH12: Factory metadata matches spec.

// Must come before any include that transitively pulls in distingnt/slot.h,
// to provide NULL for the slot.h constructor on macOS clang++.
#include <cstddef>

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "nt_jsonstream.h"

#include <distingnt/api.h>
#include "HemiPluginInterface.h"
#include <cstdint>
#include <cstring>
#include <string>

// Declarations for the test-injection API exported by Quadrants_host.cpp
// under NT_HEM_HOST_SIM.
extern "C" {
void    qq_test_inject_slot(int slot_idx, HemiPluginInterface* plugin, uint32_t guid);
void    qq_test_clear_slots(void);
uint8_t qq_test_get_focused_slot(const _NT_algorithm* self);
void    qq_test_set_focused_slot(_NT_algorithm* self, uint8_t idx);
}

// ---------------------------------------------------------------------------
// _NT_slot stubs live in harness/src/nt_runtime.cpp (integration-owned).

// ---------------------------------------------------------------------------
// Stub HemiPluginInterface helpers
// ---------------------------------------------------------------------------

namespace {

// Per-slot call counters reset between tests.
struct StubCounters {
    int render_view_calls        = 0;
    int encoder_turn_calls       = 0;
    int encoder_turn_last_dir    = 0;
    int encoder_shifted_calls    = 0;
    int encoder_shifted_last_dir = 0;
    int button_press_calls       = 0;
    int aux_button_calls         = 0;
};

// Four slots worth of counters.
StubCounters g_counts[4];

void reset_counters() {
    for (int i = 0; i < 4; ++i) g_counts[i] = StubCounters{};
}

// Each stub instance carries a slot_index so its handlers can update the
// matching g_counts[slot_index] entry.
struct StubInstance : public HemiPluginInterface {
    int slot_index;
};

static void render_view_fn(_NT_algorithm* self, int, int) {
    g_counts[static_cast<StubInstance*>(self)->slot_index].render_view_calls++;
}
static void encoder_turn_fn(_NT_algorithm* self, int dir) {
    auto& c = g_counts[static_cast<StubInstance*>(self)->slot_index];
    c.encoder_turn_calls++;
    c.encoder_turn_last_dir = dir;
}
static void encoder_shifted_fn(_NT_algorithm* self, int dir) {
    auto& c = g_counts[static_cast<StubInstance*>(self)->slot_index];
    c.encoder_shifted_calls++;
    c.encoder_shifted_last_dir = dir;
}
static void button_press_fn(_NT_algorithm* self) {
    g_counts[static_cast<StubInstance*>(self)->slot_index].button_press_calls++;
}
static void aux_button_fn(_NT_algorithm* self) {
    g_counts[static_cast<StubInstance*>(self)->slot_index].aux_button_calls++;
}

StubInstance g_stubs[4];

void init_stub(int i,
               uint32_t magic   = kHemiInterfaceMagic,
               uint32_t version = kHemiInterfaceVersion) {
    StubInstance& s          = g_stubs[i];
    s.parameters             = nullptr;
    s.parameterPages         = nullptr;
    s.vIncludingCommon       = nullptr;
    s.v                      = nullptr;
    s.magic                  = magic;
    s.interface_version      = version;
    s.render_view            = render_view_fn;
    s.on_encoder_turn        = encoder_turn_fn;
    s.on_encoder_turn_shifted = encoder_shifted_fn;
    s.on_button_press        = button_press_fn;
    s.on_aux_button          = aux_button_fn;
    s.slot_index             = i;
}

// Install all 4 stubs as valid HemiPluginInterface instances.
void inject_all_valid_stubs(uint32_t guid_base = NT_MULTICHAR('H','m','X','x')) {
    for (int i = 0; i < 4; ++i) {
        init_stub(i);
        qq_test_inject_slot(i, &g_stubs[i], guid_base);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Test fixture helpers
// ---------------------------------------------------------------------------

static nt::LoadedPlugin* setup_host() {
    nt::reset_plugin_loader();
    qq_test_clear_slots();
    reset_counters();
    nt::LoadedPlugin* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    return loaded;
}

// Build a _NT_uiData with a rising edge on the given controls bitmask.
static _NT_uiData button_edge(uint16_t which) {
    _NT_uiData d{};
    d.controls    = which;
    d.lastButtons = 0;
    return d;
}

// Build a _NT_uiData for an encoder turn.
static _NT_uiData encoder_turn(int enc_idx, int8_t delta) {
    _NT_uiData d{};
    d.encoders[enc_idx] = delta;
    return d;
}

// Build a _NT_uiData for an encoder button rising edge.
static _NT_uiData encoder_button_edge(uint16_t which) {
    _NT_uiData d{};
    d.controls    = which;
    d.lastButtons = 0;
    return d;
}

// ---------------------------------------------------------------------------
// QH1a-d: button1-4 sets focused slot index
// ---------------------------------------------------------------------------

TEST_CASE("QH1a: button1 edge sets focused slot to 0", "[quadrants-host]") {
    auto* loaded = setup_host();
    qq_test_set_focused_slot(loaded->algorithm, 3);

    loaded->factory->customUi(loaded->algorithm, button_edge(kNT_button1));

    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 0);
    qq_test_clear_slots();
}

TEST_CASE("QH1b: button2 edge sets focused slot to 1", "[quadrants-host]") {
    auto* loaded = setup_host();

    loaded->factory->customUi(loaded->algorithm, button_edge(kNT_button2));

    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 1);
    qq_test_clear_slots();
}

TEST_CASE("QH1c: button3 edge sets focused slot to 2", "[quadrants-host]") {
    auto* loaded = setup_host();

    loaded->factory->customUi(loaded->algorithm, button_edge(kNT_button3));

    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 2);
    qq_test_clear_slots();
}

TEST_CASE("QH1d: button4 edge sets focused slot to 3", "[quadrants-host]") {
    auto* loaded = setup_host();

    loaded->factory->customUi(loaded->algorithm, button_edge(kNT_button4));

    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 3);
    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH2: L encoder routes to focused slot on_encoder_turn only
// ---------------------------------------------------------------------------

TEST_CASE("QH2: L encoder turn routes only to focused slot", "[quadrants-host]") {
    auto* loaded = setup_host();
    inject_all_valid_stubs();

    // Focus slot 2.
    qq_test_set_focused_slot(loaded->algorithm, 2);

    loaded->factory->customUi(loaded->algorithm, encoder_turn(0, 1));

    // Only slot 2 receives the encoder turn.
    REQUIRE(g_counts[2].encoder_turn_calls == 1);
    REQUIRE(g_counts[2].encoder_turn_last_dir == 1);
    REQUIRE(g_counts[0].encoder_turn_calls == 0);
    REQUIRE(g_counts[1].encoder_turn_calls == 0);
    REQUIRE(g_counts[3].encoder_turn_calls == 0);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH3: R encoder routes to focused slot via on_encoder_turn_shifted
// ---------------------------------------------------------------------------

TEST_CASE("QH3: R encoder turn routes only to focused slot as shifted", "[quadrants-host]") {
    auto* loaded = setup_host();
    inject_all_valid_stubs();

    // Focus slot 1.
    qq_test_set_focused_slot(loaded->algorithm, 1);

    loaded->factory->customUi(loaded->algorithm, encoder_turn(1, -1));

    REQUIRE(g_counts[1].encoder_shifted_calls == 1);
    REQUIRE(g_counts[1].encoder_shifted_last_dir == -1);
    // No unshifted encoder call on any slot.
    for (int i = 0; i < 4; ++i) REQUIRE(g_counts[i].encoder_turn_calls == 0);
    REQUIRE(g_counts[0].encoder_shifted_calls == 0);
    REQUIRE(g_counts[2].encoder_shifted_calls == 0);
    REQUIRE(g_counts[3].encoder_shifted_calls == 0);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH4: L encoder button edge routes to focused slot on_button_press
// ---------------------------------------------------------------------------

TEST_CASE("QH4: L encoder button edge routes to focused slot on_button_press", "[quadrants-host]") {
    auto* loaded = setup_host();
    inject_all_valid_stubs();

    qq_test_set_focused_slot(loaded->algorithm, 0);

    loaded->factory->customUi(loaded->algorithm, encoder_button_edge(kNT_encoderButtonL));

    REQUIRE(g_counts[0].button_press_calls == 1);
    REQUIRE(g_counts[1].button_press_calls == 0);
    REQUIRE(g_counts[2].button_press_calls == 0);
    REQUIRE(g_counts[3].button_press_calls == 0);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH5: R encoder button edge routes to focused slot on_aux_button
// ---------------------------------------------------------------------------

TEST_CASE("QH5: R encoder button edge routes to focused slot on_aux_button", "[quadrants-host]") {
    auto* loaded = setup_host();
    inject_all_valid_stubs();

    qq_test_set_focused_slot(loaded->algorithm, 3);

    loaded->factory->customUi(loaded->algorithm, encoder_button_edge(kNT_encoderButtonR));

    REQUIRE(g_counts[3].aux_button_calls == 1);
    REQUIRE(g_counts[0].aux_button_calls == 0);
    REQUIRE(g_counts[1].aux_button_calls == 0);
    REQUIRE(g_counts[2].aux_button_calls == 0);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH6: ABI mismatch -- stub with magic=0 in focused slot: no routing, no crash
// ---------------------------------------------------------------------------

TEST_CASE("QH6: ABI mismatch (magic=0) in focused slot causes no routing", "[quadrants-host]") {
    auto* loaded = setup_host();

    // Slots 0, 1, 3 valid; slot 2 has wrong magic.
    for (int i : {0, 1, 3}) {
        init_stub(i);
        qq_test_inject_slot(i, &g_stubs[i], NT_MULTICHAR('H','m','X','x'));
    }
    init_stub(2, 0 /* bad magic */, kHemiInterfaceVersion);
    qq_test_inject_slot(2, &g_stubs[2], NT_MULTICHAR('H','m','X','x'));

    // Focus slot 2 and fire encoder -- no crash, no routing.
    qq_test_set_focused_slot(loaded->algorithm, 2);
    loaded->factory->customUi(loaded->algorithm, encoder_turn(0, 1));

    // No slot received a call.
    for (int i = 0; i < 4; ++i) REQUIRE(g_counts[i].encoder_turn_calls == 0);

    // draw() must not crash.
    loaded->factory->draw(loaded->algorithm);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH7: Empty (nullptr) slot in focused position: no crash
// ---------------------------------------------------------------------------

TEST_CASE("QH7: nullptr plugin in focused slot causes no routing, no crash", "[quadrants-host]") {
    auto* loaded = setup_host();

    // Slots 0, 2, 3 valid; slot 1 injected with nullptr plugin (failed validation).
    for (int i : {0, 2, 3}) {
        init_stub(i);
        qq_test_inject_slot(i, &g_stubs[i], NT_MULTICHAR('H','m','X','x'));
    }
    qq_test_inject_slot(1, nullptr, 0);  // empty slot

    qq_test_set_focused_slot(loaded->algorithm, 1);
    loaded->factory->customUi(loaded->algorithm, encoder_turn(0, 1));

    for (int i = 0; i < 4; ++i) REQUIRE(g_counts[i].encoder_turn_calls == 0);

    loaded->factory->draw(loaded->algorithm);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH8: All slots empty -- draw renders stubs, no crash
// ---------------------------------------------------------------------------

TEST_CASE("QH8: all slots empty draws incompatible stubs without crash", "[quadrants-host]") {
    auto* loaded = setup_host();
    // No qq_test_inject_slot calls; s_test_slots_active is false so resolve
    // falls through to NT_getSlot which returns false.
    loaded->factory->draw(loaded->algorithm);
    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH9: focused-slot serialise/deserialise round-trip
// ---------------------------------------------------------------------------

TEST_CASE("QH9: serialise/deserialise round-trip preserves focused_slot_idx", "[quadrants-host]") {
    auto* loaded = setup_host();

    qq_test_set_focused_slot(loaded->algorithm, 3);
    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 3);

    // Serialise.
    auto stream = nt::make_json_stream();
    stream->openObject();
    loaded->factory->serialise(loaded->algorithm, *stream);
    stream->closeObject();
    const std::string& json = stream->buffer();
    REQUIRE(!json.empty());

    // Reset and deserialise.
    qq_test_set_focused_slot(loaded->algorithm, 0);
    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 0);

    auto parse = nt::make_json_parse(json);
    bool ok = loaded->factory->deserialise(loaded->algorithm, *parse);
    REQUIRE(ok);

    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 3);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH10: hasCustomUi returns full bitmask
// ---------------------------------------------------------------------------

TEST_CASE("QH10: hasCustomUi returns full Quadrants claim bitmask", "[quadrants-host]") {
    auto* loaded = setup_host();
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    REQUIRE((mask & kNT_button1) != 0u);
    REQUIRE((mask & kNT_button2) != 0u);
    REQUIRE((mask & kNT_button3) != 0u);
    REQUIRE((mask & kNT_button4) != 0u);
    REQUIRE((mask & kNT_encoderL) != 0u);
    REQUIRE((mask & kNT_encoderR) != 0u);
    REQUIRE((mask & kNT_encoderButtonL) != 0u);
    REQUIRE((mask & kNT_encoderButtonR) != 0u);
    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH11: Holding button1 (no rising edge) does not re-trigger focus change
// ---------------------------------------------------------------------------

TEST_CASE("QH11: held button1 (no rising edge) does not re-trigger focus change",
          "[quadrants-host]") {
    auto* loaded = setup_host();

    // Rising edge: set focus to 0.
    loaded->factory->customUi(loaded->algorithm, button_edge(kNT_button1));
    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 0);

    // Move focus elsewhere.
    qq_test_set_focused_slot(loaded->algorithm, 3);

    // Held state: both controls and lastButtons have button1 set (no rising edge).
    _NT_uiData held{};
    held.controls    = kNT_button1;
    held.lastButtons = kNT_button1;
    loaded->factory->customUi(loaded->algorithm, held);

    // Focus must remain at 3.
    REQUIRE(qq_test_get_focused_slot(loaded->algorithm) == 3);

    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH12: Factory metadata matches spec
// ---------------------------------------------------------------------------

TEST_CASE("QH12: factory metadata matches spec", "[quadrants-host]") {
    auto* loaded = setup_host();
    REQUIRE(loaded->factory->guid == NT_MULTICHAR('H','m','Q','q'));
    REQUIRE(std::string(loaded->factory->name) == "Quadrants Host");
    REQUIRE(std::string(loaded->factory->description) ==
            "Composes 4 Hemi applets with focused-slot control");
    qq_test_clear_slots();
}

// ---------------------------------------------------------------------------
// QH13: draw() calls render_view on valid slots and skips incompatible
// ---------------------------------------------------------------------------

TEST_CASE("QH13: draw routes render_view to valid slots only", "[quadrants-host]") {
    auto* loaded = setup_host();

    // Slots 0 and 3 valid; slots 1 and 2 empty.
    for (int i : {0, 3}) {
        init_stub(i);
        qq_test_inject_slot(i, &g_stubs[i], NT_MULTICHAR('H','m','X','x'));
    }
    qq_test_inject_slot(1, nullptr, 0);
    qq_test_inject_slot(2, nullptr, 0);

    loaded->factory->draw(loaded->algorithm);

    REQUIRE(g_counts[0].render_view_calls == 1);
    REQUIRE(g_counts[1].render_view_calls == 0);
    REQUIRE(g_counts[2].render_view_calls == 0);
    REQUIRE(g_counts[3].render_view_calls == 1);

    qq_test_clear_slots();
}
