#pragma once
#include <Arduino.h>

#include "../../TinyWifiDongleConfig.h"
#include "../../core/L2Bridge.h"
#include "Rp2040SerialLogger.h"
#include "Rp2040UsbNcm.h"
#include "Rp2040WifiFrameLink.h"

namespace twd {

/**
 * @brief Raspberry Pi Pico W / Pico 2 W dongle: raw Ethernet frames between USB and WiFi.
 *
 *     Rp2040UsbNcm (UsbTransport) <-> L2Bridge <-> Rp2040WifiFrameLink (FrameLink)
 *
 * The USB adapter takes the WiFi chip's station MAC address; the host does its
 * own DHCP/DNS with the WiFi network. The board has no IP address of its own.
 * Needs the core option "USB Stack: Pico SDK" (the default).
 *
 * The settings manageUsb, pinUsbTask, stallTimeoutMs and usbMaxFrame of the
 * configuration only apply to the ESP32 and are ignored here; framesToHost and
 * framesFromHost only apply here.
 */
class Rp2040Dongle {
 public:
  Rp2040Dongle() : usb_(log_), link_(log_), bridge_(usb_, link_, log_) {}

  /// Start USB and WiFi and bridge them; false if a part could not be started.
  bool begin(const TinyWifiDongleConfig& cfg) {
    if (!cfg.ssid) return false;
    log_.begin(cfg.logTarget);
    log_.log("starting: ssid='%s'", cfg.ssid);

    usb_.setBuffers(cfg.framesToHost, cfg.framesFromHost);
    usb_.setHostMac(link_.macAddress());  // the access point only accepts the station's MAC
    if (!usb_.begin(cfg.productName)) return false;
    bridge_.begin();
    link_.begin(cfg.ssid, cfg.password);

    if (log_.enabled() && cfg.statsIntervalMs) {
      statsMs_ = cfg.statsIntervalMs;
      stats_.user_data = this;
      stats_.do_work = &Rp2040Dongle::statsCb;
      async_context_add_at_time_worker_in_ms(__getEthernetContext(), &stats_, statsMs_);
    }
    return true;
  }

  /// True once the USB host has configured the adapter.
  bool usbReady() { return usb_.ready(); }
  /// True while the board is associated with the access point.
  bool uplinkConnected() { return link_.connected(); }

  /// Counters of the bridge since start.
  L2Bridge::Stats bridgeStats() const { return bridge_.stats(); }
  /// Counters of the USB side since start.
  Rp2040UsbNcm::Stats usbStats() const { return usb_.stats(); }

 private:
  Rp2040SerialLogger log_;
  Rp2040UsbNcm usb_;
  Rp2040WifiFrameLink link_;
  L2Bridge bridge_;
  async_at_time_worker_t stats_ = {};
  uint32_t statsMs_ = 0;

  static void statsCb(async_context_t*, async_at_time_worker_t* w) {
    Rp2040Dongle* s = (Rp2040Dongle*)w->user_data;
    L2Bridge::Stats b = s->bridge_.stats();
    Rp2040UsbNcm::Stats u = s->usb_.stats();
    s->log_.log("w%d toL=%u toH=%u rf=%u dr=%u | u rx=%u tx=%u td=%u rd=%u q=%u", (int)s->link_.connected(),
                (unsigned)b.toLink, (unsigned)b.toHost, (unsigned)b.reflected, (unsigned)b.dropped,
                (unsigned)u.rxFrames, (unsigned)u.txFrames, (unsigned)u.txDrops, (unsigned)u.rxDrops,
                (unsigned)s->usb_.txQueued());
    w->next_time = make_timeout_time_ms(s->statsMs_);
  }
};

}  // namespace twd
