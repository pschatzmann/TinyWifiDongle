#pragma once
#include <Arduino.h>

#include "../../TinyWifiDongleConfig.h"
#include "../../core/Interfaces.h"

namespace twd {

/**
 * @brief Logger that writes to the USB serial port (`Serial`) or to the first hardware UART (`Serial1`).
 *
 * Never blocks when nobody listens: a line is dropped if the port cannot take
 * it at once. Until begin() is called, and for LogTarget::None, it discards
 * everything.
 *
 * - USB serial port: a second function of the USB connection, next to the
 *   network adapter. It also lets the board be uploaded to without BOOTSEL.
 * - UART: `Serial1` on its default pins (GP0 transmit, GP1 receive) at 115200
 *   baud, for an external USB-serial adapter. Keeps the USB connection free of any serial port.
 */
class Rp2040SerialLogger : public Logger {
 public:
  /// The target that LogTarget::Default stands for on this chip.
  static LogTarget resolve(LogTarget target) { return target == LogTarget::Default ? LogTarget::UsbCdc : target; }

  /// Starts the selected port.
  void begin(LogTarget target) {
    target = resolve(target);
    if (target == LogTarget::None) return;
    if (target == LogTarget::Uart0) {
      Serial1.begin(115200);
      out_ = &Serial1;
      room_ = [](Print* p) { return ((SerialUART*)p)->availableForWrite(); };
    } else {
      Serial.begin(115200);
      out_ = &Serial;
      room_ = [](Print* p) { return ((SerialUSB*)p)->availableForWrite(); };
    }
  }

  /// True once begin() has set up a port.
  bool enabled() override { return out_ != nullptr; }

 protected:
  /// Writes the line unless the port is full.
  void write(const char* line) override {
    size_t need = strlen(line) + 12;
    if (room_(out_) < (int)need) return;
    out_->print("[dongle] ");
    out_->println(line);
  }

 private:
  Print* out_ = nullptr;
  int (*room_)(Print*) = nullptr;
};

}  // namespace twd
