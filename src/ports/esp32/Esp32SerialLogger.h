#pragma once
#include <Arduino.h>
#include "USB.h"
#include "USBCDC.h"
#include "tusb.h"
#include "device/usbd_pvt.h"  // usbd_defer_func

#include "../../TinyWifiDongleConfig.h"
#include "../../core/Interfaces.h"

namespace twd {

/**
 * @brief Logger that writes to UART0 ("Serial0") or to a USB serial port (CDC).
 *
 * Never blocks when nobody listens. Until begin() is called, and for
 * LogTarget::None, it discards everything.
 *
 * - UART0: the text is written directly, guarded by a mutex. Needs no USB at
 *   all, and is the choice on boards with a USB-UART bridge.
 * - USB serial port (CDC): any task may call log(), but the text is queued
 *   and written from the TinyUSB task. Arming the serial port's USB endpoint
 *   from another CPU core than the USB interrupt races with the interrupt in
 *   the Arduino core's TinyUSB driver and can stall the network send path (see
 *   docs/architecture.md). The port also lets the board be reset and flashed
 *   without BOOT+RESET.
 */
class Esp32SerialLogger : public Logger {
 public:
  /// The target that LogTarget::Default stands for on this chip.
  static LogTarget resolve(LogTarget target) {
    if (target != LogTarget::Default) return target;
#if CONFIG_IDF_TARGET_ESP32P4
    return LogTarget::Uart0;
#else
    return LogTarget::UsbCdc;
#endif
  }

  /// Starts the selected port. For the USB serial port this must run before the USB stack is started.
  void begin(LogTarget target) {
    target = resolve(target);
    if (target == LogTarget::None) return;
    lock_ = xSemaphoreCreateMutex();
    if (target == LogTarget::Uart0) {
      Serial0.begin(115200);
      out_ = &Serial0;
      return;
    }
    queue_ = xQueueCreate(kQueueLen, sizeof(Line));
    usb_ = true;
#if ARDUINO_USB_CDC_ON_BOOT
    out_ = &Serial;  // the core already runs a CDC port as Serial
#else
    cdc_.setTxTimeoutMs(0);
    cdc_.begin();
    out_ = &cdc_;
#endif
  }

  /// True once begin() has set up a port.
  bool enabled() override { return out_ && lock_ && (!usb_ || queue_); }

 protected:
  /// UART: writes the line. USB: queues it, dropped if the queue is full or USB is not running yet.
  void write(const char* line) override {
    if (!usb_) {
      if (xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) != pdTRUE) return;
      out_->print("[dongle] ");
      out_->println(line);
      xSemaphoreGive(lock_);
      return;
    }
    if (!tusb_inited()) return;
    Line l;
    l.len = (uint8_t)strnlen(line, sizeof(l.text) - 1);
    memcpy(l.text, line, l.len);
    if (xQueueSend(queue_, &l, 0) != pdTRUE) return;
    if (!__atomic_exchange_n(&scheduled_, true, __ATOMIC_ACQ_REL)) {
      usbd_defer_func(&Esp32SerialLogger::drainCb, this, false);
    }
  }

 private:
  static constexpr int kQueueLen = 16;
  struct Line {
    uint8_t len;
    char text[159];
  };

  USBCDC cdc_{0};
  Print* out_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;
  bool usb_ = false;  // writes go through the TinyUSB task
  volatile bool scheduled_ = false;

  static void drainCb(void* arg) { ((Esp32SerialLogger*)arg)->drain(); }

  /// TinyUSB task.
  void drain() {
    scheduled_ = false;
    Line l;
    while (xQueueReceive(queue_, &l, 0) == pdTRUE) {
      out_->print("[dongle] ");
      out_->write((const uint8_t*)l.text, l.len);
      out_->print("\r\n");
    }
  }
};

}  // namespace twd
