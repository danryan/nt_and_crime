#pragma once

// Reusable _NT_uiData synthesis helper for O_C apps host tests.
//
// The per-app runtime (plugins/apps/_per_app_runtime.h) routes firmware
// control input through customUi(alg, _NT_uiData), tracking per-button
// held_since timestamps and the live controls mask. The actual UI::Event
// emission lives in each per-app .cpp (vendor ui_events.h puts Event in
// top-level ::UI::, which the runtime header deliberately does not pull). The
// harness never builds a _NT_uiData on its own, so this header gives tests a
// small driver to:
//
//   * build a _NT_uiData snapshot from a held-button set, a previous-frame
//     held set (for lastButtons), and encoder deltas;
//   * drive press / long_press / turn_encoder / chord sequences through the
//     runtime's customUi while advancing OC::CORE::ticks so the time-based
//     long-press logic sees realistic timing;
//   * read back the classification the runtime would report for the most
//     recent release, the synthesized _NT_uiData, and whether a long press
//     elapsed during the most recent hold.
//
// The foundation router test (test_oc_router.cpp) asserts on the runtime
// primitives the per-app TU reads. The per-app router tests (Low-rents,
// Harrington1200) reuse the builder and the down/up/turn sequences to drive
// their own HandleButtonEvent / HandleEncoderEvent through a per-app customUi.
//
// This header is host-test infrastructure. It is not included by any ARM
// plug-in TU and pulls only the runtime header plus the vendor control bits.

#include <distingnt/api.h>
#include <cstdint>
#include <initializer_list>

#include "OC_core.h"
#include "../../plugins/apps/_per_app_runtime.h"

namespace oc_ui_sim {

// Which encoder a turn_encoder() call drives. Maps to the _NT_uiData
// encoders[] index: L -> 0, R -> 1.
enum Encoder { ENCODER_L = 0, ENCODER_R = 1 };

// Ticks the press()/long_press() helpers advance OC::CORE::ticks between the
// down and up edges. A short press stays well under kLongPressTicks; a long
// press crosses it.
constexpr uint64_t kShortHoldTicks = 8;
constexpr uint64_t kLongHoldTicks  = oc_runtime::kLongPressTicks + 1;

// ---------------------------------------------------------------------------
// Snapshot builder
// ---------------------------------------------------------------------------

// Build a _NT_uiData from the currently-held control bits, the previous-frame
// held bits (so the runtime's edge detector `controls XOR lastButtons` sees the
// right transitions), and the two encoder deltas.
inline _NT_uiData make_uidata(uint16_t held, uint16_t prev_held,
                              int8_t enc_l, int8_t enc_r) {
    _NT_uiData d{};
    d.controls    = held;
    d.lastButtons = prev_held;
    d.encoders[0] = enc_l;
    d.encoders[1] = enc_r;
    return d;
}

// ---------------------------------------------------------------------------
// Recorded state for assertions
// ---------------------------------------------------------------------------

// Last _NT_uiData synthesized and fed to the runtime. Tests inspect this for
// encoder deltas (the runtime does not store encoder deltas; the per-app TU
// reads data.encoders[] directly to emit EVENT_ENCODER).
inline _NT_uiData& last_uidata_ref() {
    static _NT_uiData d{};
    return d;
}
inline const _NT_uiData& last_uidata() { return last_uidata_ref(); }

// Classification the runtime reported for the most recent release (PRESS for a
// short release, LONG_RELEASE for a release after a long hold).
inline uint8_t& last_release_class_ref() {
    static uint8_t c = oc_runtime::EVENT_NONE;
    return c;
}
inline uint8_t last_release_class() { return last_release_class_ref(); }

// Whether the runtime saw the long-press threshold elapse during the most
// recent hold (sampled just before the release edge).
inline bool& last_long_press_seen_ref() {
    static bool b = false;
    return b;
}
inline bool last_long_press_seen() { return last_long_press_seen_ref(); }

// ---------------------------------------------------------------------------
// Sequence drivers
// ---------------------------------------------------------------------------

namespace detail {

// Feed one snapshot through the runtime's customUi and record it.
inline void feed(oc_runtime::AppAlgorithm& alg, const _NT_uiData& d) {
    last_uidata_ref() = d;
    oc_runtime::customUi(alg, d);
}

// Drive a down edge then an up edge on a single control bit, holding it for
// `hold_ticks` worth of OC::CORE::ticks in between. Records the runtime's
// long-press observation and release classification sampled just before the
// up edge, mirroring what a per-app customUi reads to compose its UI::Event.
inline void press_for(oc_runtime::AppAlgorithm& alg, uint16_t bit,
                      uint64_t hold_ticks) {
    const int bi = oc_runtime::bit_index(bit);

    const uint16_t prev = oc_runtime::last_controls_of(alg);
    feed(alg, make_uidata(static_cast<uint16_t>(prev | bit), prev, 0, 0));

    OC::CORE::ticks += hold_ticks;

    last_long_press_seen_ref() =
        oc_runtime::was_long_press_already_emitted(&alg, bi);
    last_release_class_ref() = oc_runtime::classify_release(&alg, bi);

    const uint16_t held_now = oc_runtime::last_controls_of(alg);
    feed(alg, make_uidata(static_cast<uint16_t>(held_now & ~bit), held_now, 0, 0));
}

}  // namespace detail

// Short press: down then up within the long-press window.
inline void press(oc_runtime::AppAlgorithm& alg, uint16_t bit) {
    detail::press_for(alg, bit, kShortHoldTicks);
}

// Long press: down, advance time past kLongPressTicks while held, then up.
inline void long_press(oc_runtime::AppAlgorithm& alg, uint16_t bit) {
    detail::press_for(alg, bit, kLongHoldTicks);
}

// Encoder turn: synthesize a snapshot carrying the delta on the selected
// encoder index and feed it through the runtime. The runtime resets its idle
// counter on encoder activity; the delta itself is consumed by the per-app TU,
// so tests read it back via last_uidata().
inline void turn_encoder(oc_runtime::AppAlgorithm& alg, Encoder which,
                         int delta) {
    const uint16_t held = oc_runtime::last_controls_of(alg);
    const int8_t d = static_cast<int8_t>(delta);
    if (which == ENCODER_L) {
        detail::feed(alg, make_uidata(held, held, d, 0));
    } else {
        detail::feed(alg, make_uidata(held, held, 0, d));
    }
}

// Chord: press multiple buttons down simultaneously. After this call the
// runtime's last_controls carries every chord bit, which is the .mask a
// per-app TU stamps onto each event. The bits are left held; a follow-up
// press()/release on the same algorithm clears them.
inline void chord(oc_runtime::AppAlgorithm& alg,
                  std::initializer_list<uint16_t> bits) {
    const uint16_t prev = oc_runtime::last_controls_of(alg);
    uint16_t held = prev;
    for (uint16_t b : bits) held = static_cast<uint16_t>(held | b);
    detail::feed(alg, make_uidata(held, prev, 0, 0));
}

}  // namespace oc_ui_sim
