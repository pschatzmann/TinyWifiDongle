#pragma once
/**
 * @file TinyWifiDongle.h
 * @brief Header-only USB WiFi dongle for Arduino.
 *
 * The microcontroller joins a WiFi network and presents itself to a host
 * computer as a USB network adapter (CDC-NCM). Ethernet frames are forwarded
 * unchanged between USB and WiFi (a layer-2 bridge), so the host is a normal
 * client of the WiFi network and gets its address from that network's DHCP
 * server. The MCU itself has no IP address:
 *
 *   Host PC <-USB NCM-> [ L2 bridge ] <-> [ WiFi STA ] <-> access point
 *
 * Each MCU family supplies a "port" (ports/TinyWifiDonglePort{Mcu}.h) with
 * the same small contract:
 *
 *   bool begin(const TinyWifiDongleConfig&)  start USB and WiFi, bridge them
 *   bool usbReady()                          host has enumerated the device
 *   bool uplinkConnected()                   WiFi is associated
 *
 * Because the library is header-only and defines USB driver callbacks, include
 * it from exactly one translation unit (the sketch's .ino).
 */
#include "TinyWifiDongleConfig.h"

#if defined(ESP32)
#include "ports/TinyWifiDonglePortEsp32.h"
#elif defined(ARDUINO_ARCH_RP2040)
#include "ports/TinyWifiDonglePortRp2040.h"
#else
#error "TinyWifiDongle: unsupported architecture (supported: ESP32-S2/S3; planned: RP2040 W)"
#endif

/**
 * @brief Shares the microcontroller's WiFi connection with a USB host.
 *
 * Thin facade over the port of the MCU in use (TinyWifiDonglePort): the
 * microcontroller joins a WiFi network and appears to the host as a USB
 * network adapter that is bridged to the WiFi network.
 *
 * @code
 * TinyWifiDongle dongle;
 * void setup() { dongle.begin("ssid", "password"); }
 * void loop() {}
 * @endcode
 * @see TinyWifiDongleConfig
 */
class TinyWifiDongle {
 public:
  /// Join `ssid` and expose the connection over USB.
  bool begin(const char* ssid, const char* password = nullptr) {
    TinyWifiDongleConfig cfg;
    cfg.ssid = ssid;
    cfg.password = password;
    return begin(cfg);
  }

  /// Start the dongle with full control over all settings.
  bool begin(const TinyWifiDongleConfig& cfg) { return port_.begin(cfg); }

  /// True once the USB host has enumerated and configured the adapter.
  bool isUsbReady() { return port_.usbReady(); }

  /// True when the MCU is associated with the WiFi access point.
  bool isWifiConnected() { return port_.uplinkConnected(); }

 protected:
  /// The port of the MCU in use; does all the work.
  TinyWifiDonglePort port_;
};
