#pragma once
#include <Arduino.h>
#include "tusb.h"

namespace twd {

/**
 * @brief Runs the TinyUSB device task on one fixed CPU core.
 *
 * The Arduino core creates its "usbd" task without a core affinity, so it may
 * run on the other core than the USB interrupt. TinyUSB's DWC2 driver of the
 * ESP32-S2/S3 (0.21.0) has races between that task and the interrupt when
 * they run on different cores: a bulk IN transfer can lose its "TX FIFO empty"
 * interrupt and never complete, which stalls the network send path (see
 * docs/architecture.md). This class waits until the core's task is idle,
 * deletes it and starts an identical one (`for (;;) tud_task();`) pinned to
 * `core`, which should be the core that started the USB stack, because the
 * USB interrupt is allocated on that core.
 */
class Esp32UsbTaskPin {
 public:
  /// Pin the USB task to `core` (0 or 1); call right after USB.begin().
  bool begin(int core) {
    core_ = core;
    return xTaskCreatePinnedToCore(&Esp32UsbTaskPin::swap, "usbPin", 3072, this, 5, nullptr, core) == pdPASS;
  }

  /// True once the pinned task is running.
  bool pinned() const { return pinned_; }

 private:
  int core_ = 0;
  volatile bool pinned_ = false;

  static void usbdTask(void*) {
    for (;;) tud_task();
  }

  static void swap(void* arg) {
    Esp32UsbTaskPin* s = (Esp32UsbTaskPin*)arg;
    for (int tries = 0; tries < 200 && !s->pinned_; tries++) {
      vTaskDelay(pdMS_TO_TICKS(50));
      TaskHandle_t h = xTaskGetHandle("usbd");
      if (!h) continue;
      // Only take it over while it waits for events, not in the middle of a callback.
      if (eTaskGetState(h) != eBlocked) continue;
      vTaskSuspend(h);
      vTaskDelete(h);
      xTaskCreatePinnedToCore(&Esp32UsbTaskPin::usbdTask, "usbd", 4096, nullptr, configMAX_PRIORITIES - 1, nullptr,
                              s->core_);
      s->pinned_ = true;
    }
    vTaskDelete(nullptr);
  }
};

}  // namespace twd
