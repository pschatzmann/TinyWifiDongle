#pragma once
// Portable configuration shared by all ports.
#include <stdint.h>

/// Where the library prints its messages and the optional status line.
enum class LogTarget {
  /// UART0 ("Serial0") on the ESP32-P4, whose dev boards have a USB-UART bridge;
  /// a serial port on the USB connection (UsbCdc) on the ESP32-S2/S3.
  Default,
  /// No output and no extra USB serial port.
  None,
  /// A USB serial port (CDC) next to the network adapter. It also lets the
  /// board be flashed without BOOT+RESET (ESP32-S2/S3).
  UsbCdc,
  /// Hardware serial port UART0 ("Serial0", 115200 baud), e.g. the USB-UART
  /// bridge of a dev board. Keeps the USB connection free of any serial port.
  Uart0,
};

/**
 * @brief Settings of the dongle; every field has a usable default except the SSID.
 *
 * Pass it to TinyWifiDongle::begin(). String pointers only have to stay valid
 * during that call.
 */
struct TinyWifiDongleConfig {
  /// WiFi network the MCU joins (the "uplink").
  const char* ssid = nullptr;
  /// Password of the network (nullptr for an open network).
  const char* password = nullptr;

  /// USB product string shown on the host.
  const char* productName = "TinyWifiDongle";

  /// true: the library calls USB.begin() itself. Set to false if the sketch
  /// adds other USB interfaces (e.g. CDC for logging) and starts USB later;
  /// begin() then only registers the network interface.
  bool manageUsb = true;

  /// Where messages are printed, see LogTarget. The core's own log_x() output
  /// is never redirected.
  LogTarget logTarget = LogTarget::Default;

  /// Interval in milliseconds of the status line on the log target; 0 = never
  /// (default). Only useful while debugging.
  uint32_t statsIntervalMs = 0;

  /// If sending to the host makes no progress for this many milliseconds, the
  /// device is disconnected from USB and re-enumerated (the host sees the card
  /// being replugged and restarts the connection). 0 = never reset.
  uint32_t stallTimeoutMs = 1000;

  /// Run the TinyUSB device task on the CPU core that runs the USB interrupt.
  /// The Arduino core starts that task without a core affinity; the TinyUSB
  /// driver of the ESP32-S2/S3 (0.21.0) has cross-core races between task and
  /// interrupt that make the send path stall under load. Keep this on unless
  /// you start the USB stack yourself (needs manageUsb).
  bool pinUsbTask = true;

  /// Largest Ethernet frame the USB adapter announces to the host (the host
  /// derives its MTU from it).
  uint16_t usbMaxFrame = 1514;
};
