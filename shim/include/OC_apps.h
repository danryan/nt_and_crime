#pragma once

#include <cstdint>

namespace OC {

enum AppEvent {
  APP_EVENT_SUSPEND,
  APP_EVENT_RESUME,
  APP_EVENT_SCREENSAVER_ON,
  APP_EVENT_SCREENSAVER_OFF
};

// Forward declaration for UI::Event
namespace UI {
  struct Event;
}

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
