#pragma once
#include <Arduino.h>
#include "USB.h"

#include "../../TinyWifiDongleConfig.h"
#include "../../core/L2Bridge.h"
#include "Esp32SerialLogger.h"
#include "Esp32UsbNcm.h"
#include "Esp32UsbTaskPin.h"
#include "Esp32WifiFrameLink.h"

namespace twd {

/**
 * @brief ESP32-S2/S3 dongle: raw Ethernet frames between USB and WiFi.
 *
 *     Esp32UsbNcm (UsbTransport) <-> L2Bridge <-> Esp32WifiFrameLink (FrameLink)
 *
 * The USB adapter takes the WiFi station's MAC address; the host does its own
 * DHCP/DNS with the WiFi network. The MCU has no IP address of its own.
 */
class Esp32Dongle {
 public:
  Esp32Dongle() : usb_(log_), link_(log_), bridge_(usb_, link_, log_) {}

  /// Start USB and WiFi and bridge them; false if a part could not be started.
  bool begin(const TinyWifiDongleConfig& cfg) {
    if (!cfg.ssid) return false;
    ssid_ = cfg.ssid;
    password_ = cfg.password ? cfg.password : "";
    product_ = cfg.productName;
    hasPassword_ = cfg.password != nullptr;

    statsIntervalMs_ = cfg.statsIntervalMs;
    log_.begin(cfg.logTarget);
    if (log_.enabled() && statsIntervalMs_) {
      xTaskCreate(&Esp32Dongle::statsTask, "dongleStats", 6144, this, 10, nullptr);
    }
    log_.log("starting: ssid='%s'", ssid_.c_str());

    usb_.setMaxFrame(cfg.usbMaxFrame);
    usb_.setStallTimeout(cfg.stallTimeoutMs);
    usb_.setHostMac(link_.macAddress());  // the access point only accepts the station's MAC
    if (!usb_.begin(product_.c_str())) return false;
    bridge_.begin();
    link_.begin(ssid_.c_str(), hasPassword_ ? password_.c_str() : nullptr);

    if (cfg.manageUsb) {
      USB.productName(product_.c_str());
      if (!USB.begin()) return false;
      if (cfg.pinUsbTask) usbPin_.begin(xPortGetCoreID());  // the USB interrupt lives on this core
    }
    return true;
  }

  /// True once the USB host has configured the adapter.
  bool usbReady() { return usb_.ready(); }
  /// True while the MCU is associated with the access point.
  bool uplinkConnected() { return link_.connected(); }

 private:
  Esp32SerialLogger log_;
  Esp32UsbNcm usb_;
  Esp32WifiFrameLink link_;
  L2Bridge bridge_;
  Esp32UsbTaskPin usbPin_;
  String ssid_, password_, product_;
  bool hasPassword_ = false;
  uint32_t statsIntervalMs_ = 0;

  static void statsTask(void* arg) {
    Esp32Dongle* s = (Esp32Dongle*)arg;
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(s->statsIntervalMs_));
      if (s->usb_.txQueued() > 8) {  // send path looks stuck: show the controller instead of the counters
        s->usb_.logControllerState();
        continue;
      }
      Esp32UsbNcm::Stats u = s->usb_.stats();
      L2Bridge::Stats b = s->bridge_.stats();
      // Short on purpose: the log port drops what does not fit its buffer.
      s->log_.log("w%d toL=%u toH=%u rf=%u dr=%u | u rx=%u tx=%u td=%u bz=%u q=%u rs=%u", (int)s->link_.connected(),
                  (unsigned)b.toLink, (unsigned)b.toHost, (unsigned)b.reflected, (unsigned)b.dropped,
                  (unsigned)u.rxFrames, (unsigned)u.txFrames, (unsigned)u.txDrops, (unsigned)u.txBusy,
                  (unsigned)s->usb_.txQueued(), (unsigned)u.resets);
    }
  }
};

}  // namespace twd
