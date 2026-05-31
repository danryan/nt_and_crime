#pragma once

// Include-guard poison (CLAUDE.md "Shadowing a vendor header quote-included
// from inside another vendor header"). A vendor app header pulled into a per-app
// TU quote-includes "OC_apps.h" from inside the vendor tree (APP_FPART.h:37),
// which resolves to the vendor sibling, not this shim shadow. Defining the
// vendor guard (OC_APP_H_) here makes that sibling self-suppress; this shim
// shadow already provides the OC::App struct and OC::apps namespace the apps use.
#ifndef OC_APP_H_
#define OC_APP_H_
#endif

#include <cstdint>

namespace OC {

enum AppEvent {
  APP_EVENT_SUSPEND,
  APP_EVENT_RESUME,
  APP_EVENT_SCREENSAVER_ON,
  APP_EVENT_SCREENSAVER_OFF
};

// UI::Event binding inside namespace OC. Two regimes, selected by whether the
// real vendor event type (::UI from UI/ui_events.h) is already in scope:
//
//   * Default (UI-free TUs, and every app whose .cpp includes ui_events.h only
//     AFTER the runtime): forward-declare a distinct incomplete OC::UI::Event.
//     OC::App's handler pointers need only an incomplete type, and per-app .cpp
//     TUs reinterpret_cast the vendor ::UI::Event-taking thunks onto these
//     pointers. Core TUs that never touch UI stay UI-free.
//
//   * When ui_events.h has already been included (a per-app .cpp that pulls
//     "UI/ui_events.h" BEFORE the runtime, e.g. PASSENCORE, which instantiates a
//     vendor UI editor like OC::ScaleEditor): alias OC::UI to the global ::UI so
//     OC::UI::Event IS ::UI::Event. A vendor editor template living in namespace
//     OC then resolves bare `UI::Event` / `UI::EVENT_*` to the complete global
//     type, and the app can pass its ::UI::Event straight into the editor. The
//     per-app reinterpret_cast onto OC::App's handler pointers degenerates to an
//     identity cast and still compiles, so this is backward-compatible.
#ifdef UI_EVENTS_H_
namespace UI = ::UI;
#else
namespace UI {
  struct Event;
}
#endif

struct App {
  uint16_t id;
  const char *name;

  void (*Init)();
  size_t (*storageSize)();
  size_t (*Save)(void *);
  size_t (*Restore)(const void *);

  void (*HandleAppEvent)(AppEvent);

  void (*loop)();
  void (*DrawMenu)();
  void (*DrawScreensaver)();

  void (*HandleButtonEvent)(const UI::Event &);
  void (*HandleEncoderEvent)(const UI::Event &);

  void (*isr)();
};

namespace apps {

  extern const App *current_app;

  void Init(bool reset_settings);

  inline void ISR() __attribute__((always_inline));
  inline void ISR() {
    if (current_app && current_app->isr)
      current_app->isr();
  }

  const App *find(uint16_t id);
  int index_of(uint16_t id);
  void set_current_app(int index);

} // namespace apps

} // namespace OC
