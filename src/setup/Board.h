#pragma once
#include <Arduino.h>

namespace twd {

/**
 * @brief The few things that differ between the supported boards.
 *
 * Used by the setup classes: reading the "forget the network" button and
 * restarting the board.
 */
class Board {
 public:
  /// True if the BOOT button (ESP32) or the BOOTSEL button (Raspberry Pi Pico) is pressed at any time during the next `ms` milliseconds.
  static bool buttonPressedWithin(uint32_t ms) {
#if defined(ESP32)
    pinMode(0, INPUT_PULLUP);  // BOOT button
#endif
    uint32_t start = millis();
    while (millis() - start < ms) {
#if defined(ESP32)
      if (digitalRead(0) == LOW) return true;
#else
      if (BOOTSEL) return true;
#endif
      delay(10);
    }
    return false;
  }

  /// Restarts the board.
  static void restart() {
#if defined(ESP32)
    ESP.restart();
#else
    rp2040.reboot();
#endif
  }
};

}  // namespace twd
