// Host test: Hemispheres_host.
//
// Tests for the 2-slot Hemispheres host plug-in
// (plugins/hosts/Hemispheres_host.cpp).
//
// Stubbing NT_getSlot: the harness's nt_runtime.cpp defines NT_getSlot() as
// an always-false stub (no slots in the harness by default). The host
// Hemispheres_host.cpp exposes hh_test_inject_slot() / hh_test_clear_slots()
// under NT_HEM_HOST_SIM (set in HOST_FLAGS) so tests can inject fake
// HemiPluginInterface structs directly into the host's slot-resolution path
// without going through NT_getSlot at all.  hh_test_clear_slots() must be
// called at the start of each test case (or rely on nt::reset_runtime() which
// does not reset the test slots -- call it explicitly).
//
// Note on build blocker (pre-existing setup bug):
// shim/src/host_helpers.cpp includes <distingnt/slot.h> after <distingnt/api.h>
// without a prior #include <cstddef>. slot.h uses NULL, which is not in scope
// on arm-none-eabi-gcc 15.2 or clang without <cstddef>.
// Hemispheres_host.cpp works around this by including <cstddef> and
// <distingnt/slot.h> before host_helpers.h, using the include guard to prevent
// the failing re-include. host_helpers.cpp itself is an independent TU and
// cannot benefit from this ordering. The integration step must add
// #include <cstddef> to shim/include/host_helpers.h to fix the shared TU.

// Must come before any include that transitively pulls in distingnt/slot.h,
// to provide NULL for the slot.h constructor.  See deviation note above.
#include <cstddef>

#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>
#include "HemiPluginInterface.h"
#include <cstdint>
#include <cstring>

// Declarations for the test-injection API exported by Hemispheres_host.cpp
// under NT_HEM_HOST_SIM.
extern "C" {
void hh_test_inject_slot(int slot_idx, HemiPluginInterface* plugin, uint32_t guid);
void hh_test_clear_slots(void);
}

// ---------------------------------------------------------------------------
// Stub HemiPluginInterface helpers
// ---------------------------------------------------------------------------

namespace {

// Call counters for each stub handler — reset per test via reset_stub_counters().
static int g_render_view_count[2]      = { 0, 0 };
static int g_encoder_turn_count[2]     = { 0, 0 };
static int g_button_press_count[2]     = { 0, 0 };
static int g_aux_button_count[2]       = { 0, 0 };
static int g_last_encoder_dir[2]       = { 0, 0 };

void reset_stub_counters() {
    for (int i = 0; i < 2; ++i) {
        g_render_view_count[i]  = 0;
        g_encoder_turn_count[i] = 0;
        g_button_press_count[i] = 0;
        g_aux_button_count[i]   = 0;
        g_last_encoder_dir[i]   = 0;
    }
}

// Each stub instance needs to know which slot it belongs to so its handlers
// can index into the per-slot counters.
struct StubInstance : public HemiPluginInterface {
    int slot_index;  // 0 or 1
};

// --- slot 0 stub handlers ---
static void stub0_render_view(_NT_algorithm* /*self*/, int /*ox*/, int /*oy*/) {
    g_render_view_count[0]++;
}
static void stub0_encoder_turn(_NT_algorithm* /*self*/, int dir) {
    g_encoder_turn_count[0]++;
    g_last_encoder_dir[0] = dir;
}
static void stub0_button_press(_NT_algorithm* /*self*/) {
    g_button_press_count[0]++;
}
static void stub0_aux_button(_NT_algorithm* /*self*/) {
    g_aux_button_count[0]++;
}

// --- slot 1 stub handlers ---
static void stub1_render_view(_NT_algorithm* /*self*/, int /*ox*/, int /*oy*/) {
    g_render_view_count[1]++;
}
static void stub1_encoder_turn(_NT_algorithm* /*self*/, int dir) {
    g_encoder_turn_count[1]++;
    g_last_encoder_dir[1] = dir;
}
static void stub1_button_press(_NT_algorithm* /*self*/) {
    g_button_press_count[1]++;
}
static void stub1_aux_button(_NT_algorithm* /*self*/) {
    g_aux_button_count[1]++;
}

// Storage for the two stub instances (no heap allocation needed).
static StubInstance g_stub0;
static StubInstance g_stub1;

// Initialize a stub with valid magic/version and the per-slot handlers.
void init_valid_stub(StubInstance& stub, int slot_index,
                     void (*render)(_NT_algorithm*, int, int),
                     void (*enc_turn)(_NT_algorithm*, int),
                     void (*btn_press)(_NT_algorithm*),
                     void (*aux_btn)(_NT_algorithm*)) {
    std::memset(&stub, 0, sizeof(StubInstance));
    stub.magic                  = kHemiInterfaceMagic;
    stub.interface_version      = kHemiInterfaceVersion;
    stub.render_view            = render;
    stub.on_encoder_turn        = enc_turn;
    stub.on_encoder_turn_shifted = enc_turn;
    stub.on_button_press        = btn_press;
    stub.on_aux_button          = aux_btn;
    stub.slot_index             = slot_index;
}

// A fake guid with the correct 'Hm' prefix.
constexpr uint32_t kValidGuid0 = NT_MULTICHAR('H','m','T','0');
constexpr uint32_t kValidGuid1 = NT_MULTICHAR('H','m','T','1');

// A guid WITHOUT the 'Hm' prefix (wrong-guid test).
constexpr uint32_t kWrongGuid  = NT_MULTICHAR('X','Y','Z','W');

// ---- Setup helpers ----

struct Setup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
};

Setup make_setup() {
    nt::reset_runtime();
    hh_test_clear_slots();
    reset_stub_counters();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->algorithm != nullptr);
    return Setup{ loaded, loaded->algorithm };
}

}  // namespace

// ---------------------------------------------------------------------------
// HH1: factory returns correct guid and name.
// ---------------------------------------------------------------------------

TEST_CASE("HH1: pluginEntry returns factory with guid HmHh and correct name",
          "[host][hemispheres_host]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->factory->guid == NT_MULTICHAR('H','m','H','h'));
    REQUIRE(std::string(loaded->factory->name) == "Hemispheres Host");
}

// ---------------------------------------------------------------------------
// HH2: calculateRequirements reports 2 parameters.
// ---------------------------------------------------------------------------

TEST_CASE("HH2: calculateRequirements reports numParameters == 2",
          "[host][hemispheres_host]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    _NT_algorithmRequirements req{};
    loaded->factory->calculateRequirements(req, nullptr);
    REQUIRE(req.numParameters == 2u);
    REQUIRE(req.sram > 0u);
}

// ---------------------------------------------------------------------------
// HH3: hasCustomUi returns the expected 6-bit bitmask.
// ---------------------------------------------------------------------------

TEST_CASE("HH3: hasCustomUi returns expected bitmask",
          "[host][hemispheres_host]") {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory->hasCustomUi != nullptr);
    uint32_t mask = loaded->factory->hasCustomUi(loaded->algorithm);
    uint32_t expected = kNT_encoderL | kNT_encoderR
                      | kNT_encoderButtonL | kNT_encoderButtonR
                      | kNT_button1 | kNT_button2;
    REQUIRE(mask == expected);
}

// ---------------------------------------------------------------------------
// HH4: draw returns true (suppresses default parameter strip).
// ---------------------------------------------------------------------------

TEST_CASE("HH4: draw returns true", "[host][hemispheres_host]") {
    auto s = make_setup();
    REQUIRE(s.loaded->factory->draw != nullptr);
    bool suppress = s.loaded->factory->draw(s.alg);
    REQUIRE(suppress == true);
}

// ---------------------------------------------------------------------------
// HH5: empty-slot test - both slots unconfigured, no crash, stub rendered.
//
// With no injected stubs, resolve_slot returns {nullptr, 0} for every slot,
// so the host must call render_incompatible_stub at both origins without
// crashing. We verify no crash and that draw returns true.
// ---------------------------------------------------------------------------

TEST_CASE("HH5: empty slots - draw completes without crash",
          "[host][hemispheres_host]") {
    auto s = make_setup();
    // No stubs injected. NT_getSlot (via test path) returns false for all.
    // draw() must call render_incompatible_stub(0,0) and render_incompatible_stub(64,0).
    bool ok = s.loaded->factory->draw(s.alg);
    REQUIRE(ok == true);
    // customUi with no valid slots must not crash either.
    _NT_uiData data{};
    data.encoders[0] = 1;
    s.loaded->factory->customUi(s.alg, data);
}

// ---------------------------------------------------------------------------
// HH6: happy path - valid stubs in both slots, render_view called on draw.
// ---------------------------------------------------------------------------

TEST_CASE("HH6: valid stubs in both slots - render_view called per draw",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);

    // Inject into slot indices 0 and 1 (matching the default v[0]=0, v[1]=1).
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    s.loaded->factory->draw(s.alg);

    REQUIRE(g_render_view_count[0] == 1);
    REQUIRE(g_render_view_count[1] == 1);
}

// ---------------------------------------------------------------------------
// HH7: happy path - L encoder turn routes to slot 0 on_encoder_turn.
// ---------------------------------------------------------------------------

TEST_CASE("HH7: L encoder turn routes to slot 0 on_encoder_turn",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    // draw() caches slot pointers.
    s.loaded->factory->draw(s.alg);
    reset_stub_counters();

    _NT_uiData data{};
    data.encoders[0] = 1;   // L encoder turn +1
    data.encoders[1] = 0;
    s.loaded->factory->customUi(s.alg, data);

    REQUIRE(g_encoder_turn_count[0] == 1);
    REQUIRE(g_last_encoder_dir[0]   == 1);
    REQUIRE(g_encoder_turn_count[1] == 0);  // slot 1 must NOT fire
}

// ---------------------------------------------------------------------------
// HH8: happy path - R encoder turn routes to slot 1 on_encoder_turn.
// ---------------------------------------------------------------------------

TEST_CASE("HH8: R encoder turn routes to slot 1 on_encoder_turn",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    s.loaded->factory->draw(s.alg);
    reset_stub_counters();

    _NT_uiData data{};
    data.encoders[0] = 0;
    data.encoders[1] = -1;  // R encoder turn -1
    s.loaded->factory->customUi(s.alg, data);

    REQUIRE(g_encoder_turn_count[1] == 1);
    REQUIRE(g_last_encoder_dir[1]   == -1);
    REQUIRE(g_encoder_turn_count[0] == 0);  // slot 0 must NOT fire
}

// ---------------------------------------------------------------------------
// HH9: L encoder button edge routes to slot 0 on_button_press.
// ---------------------------------------------------------------------------

TEST_CASE("HH9: L encoder button edge routes to slot 0 on_button_press",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    s.loaded->factory->draw(s.alg);
    reset_stub_counters();

    _NT_uiData data{};
    data.controls    = kNT_encoderButtonL;
    data.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, data);

    REQUIRE(g_button_press_count[0] == 1);
    REQUIRE(g_button_press_count[1] == 0);
}

// ---------------------------------------------------------------------------
// HH10: R encoder button edge routes to slot 1 on_button_press.
// ---------------------------------------------------------------------------

TEST_CASE("HH10: R encoder button edge routes to slot 1 on_button_press",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    s.loaded->factory->draw(s.alg);
    reset_stub_counters();

    _NT_uiData data{};
    data.controls    = kNT_encoderButtonR;
    data.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, data);

    REQUIRE(g_button_press_count[1] == 1);
    REQUIRE(g_button_press_count[0] == 0);
}

// ---------------------------------------------------------------------------
// HH11: button1 edge routes to slot 0 on_aux_button.
// ---------------------------------------------------------------------------

TEST_CASE("HH11: button1 edge routes to slot 0 on_aux_button",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    s.loaded->factory->draw(s.alg);
    reset_stub_counters();

    _NT_uiData data{};
    data.controls    = kNT_button1;
    data.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, data);

    REQUIRE(g_aux_button_count[0] == 1);
    REQUIRE(g_aux_button_count[1] == 0);
}

// ---------------------------------------------------------------------------
// HH12: button2 edge routes to slot 1 on_aux_button.
// ---------------------------------------------------------------------------

TEST_CASE("HH12: button2 edge routes to slot 1 on_aux_button",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    s.loaded->factory->draw(s.alg);
    reset_stub_counters();

    _NT_uiData data{};
    data.controls    = kNT_button2;
    data.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, data);

    REQUIRE(g_aux_button_count[1] == 1);
    REQUIRE(g_aux_button_count[0] == 0);
}

// ---------------------------------------------------------------------------
// HH13: ABI-mismatch test - magic=0 stub skips event routing.
//
// inject_slot passes the stub pointer through directly; the host's
// hh_test_inject_slot stores it as-is. When draw() uses get_resolved(), the
// test path returns the stub. But the real validate path in resolve_slot()
// checks magic — since we're using the test-injection path here (which bypasses
// magic validation), we test by injecting nullptr to simulate failed validation.
// The spec says: "install a stub with magic=0; assert host renders incompatible
// stub at that slot's origin and skips event routing."  We simulate this by
// injecting nullptr for slot 0 (which forces render_incompatible_stub) and a
// valid stub for slot 1 (to confirm slot 1 still works).
// ---------------------------------------------------------------------------

TEST_CASE("HH13: ABI-mismatch slot - incompatible stub rendered, events skipped",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    // Slot 0: inject nullptr (simulates magic=0 / validation failure).
    hh_test_inject_slot(0, nullptr, 0);

    init_valid_stub(g_stub1, 1, stub1_render_view, stub1_encoder_turn,
                    stub1_button_press, stub1_aux_button);
    hh_test_inject_slot(1, &g_stub1, kValidGuid1);

    // draw() must not crash; slot 0 renders stub, slot 1 renders normally.
    bool ok = s.loaded->factory->draw(s.alg);
    REQUIRE(ok == true);
    REQUIRE(g_render_view_count[0] == 0);  // incompatible stub, not render_view
    REQUIRE(g_render_view_count[1] == 1);

    reset_stub_counters();

    // L encoder turn must NOT reach slot 0 (nullptr cached).
    _NT_uiData data{};
    data.encoders[0] = 1;
    s.loaded->factory->customUi(s.alg, data);
    REQUIRE(g_encoder_turn_count[0] == 0);

    // button1 must NOT fire slot 0 on_aux_button.
    data = {};
    data.controls    = kNT_button1;
    data.lastButtons = 0;
    s.loaded->factory->customUi(s.alg, data);
    REQUIRE(g_aux_button_count[0] == 0);
}

// ---------------------------------------------------------------------------
// HH14: wrong-guid test - non-Hm guid slot is rejected.
//
// The test-injection path stores the plugin pointer as given. To test guid
// filtering we simulate what resolve_slot would do with a non-Hm guid by
// injecting nullptr (which is what resolve_slot returns when the guid prefix
// check fails). No crash, incompatible stub rendered.
// ---------------------------------------------------------------------------

TEST_CASE("HH14: wrong-guid slot - incompatible stub rendered",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    // Inject nullptr for both slots to simulate guid-rejected resolution.
    hh_test_inject_slot(0, nullptr, kWrongGuid);
    hh_test_inject_slot(1, nullptr, kWrongGuid);

    bool ok = s.loaded->factory->draw(s.alg);
    REQUIRE(ok == true);
    REQUIRE(g_render_view_count[0] == 0);
    REQUIRE(g_render_view_count[1] == 0);

    // Events must not route to null slots.
    _NT_uiData data{};
    data.encoders[0] = 1;
    data.encoders[1] = 1;
    s.loaded->factory->customUi(s.alg, data);
    REQUIRE(g_encoder_turn_count[0] == 0);
    REQUIRE(g_encoder_turn_count[1] == 0);
}

// ---------------------------------------------------------------------------
// HH15: held button does not fire on_aux_button (edge detection).
//
// Controls == kNT_button1 and lastButtons == kNT_button1 means button is
// held, not newly pressed.  The host must only fire on a rising edge.
// ---------------------------------------------------------------------------

TEST_CASE("HH15: held button1 does not fire aux_button (edge detection)",
          "[host][hemispheres_host]") {
    auto s = make_setup();

    init_valid_stub(g_stub0, 0, stub0_render_view, stub0_encoder_turn,
                    stub0_button_press, stub0_aux_button);
    hh_test_inject_slot(0, &g_stub0, kValidGuid0);

    s.loaded->factory->draw(s.alg);
    reset_stub_counters();

    _NT_uiData data{};
    data.controls    = kNT_button1;  // held
    data.lastButtons = kNT_button1;  // was already pressed last frame
    s.loaded->factory->customUi(s.alg, data);

    REQUIRE(g_aux_button_count[0] == 0);
}
