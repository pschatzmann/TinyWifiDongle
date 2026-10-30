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
  /// Hardware serial port UART0 (115200 baud): "Serial0" on the ESP32, "Serial1"
  /// (GP0/GP1) on the Raspberry Pi Pico. Use it with the USB-UART bridge of a
  /// dev board or an external adapter. Keeps the USB connection free of any
  /// serial port.
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

  /// Raspberry Pi Pico only: number of frame buffers (about 1.5 KB each) for
  /// frames on their way to the host. 0 = the default of the chip: 48 on the
  /// RP2350 (Pico 2 W), 24 on the RP2040 (Pico W). More buffers absorb bursts
  /// of traffic, so fewer frames are dropped (a dropped frame slows a download
  /// down), at the price of RAM. Ignored on the ESP32.
  uint8_t framesToHost = 0;

  /// Raspberry Pi Pico only: number of frame buffers for frames coming from the
  /// host (mostly acknowledgements). 0 = the default of the chip: 16 on the
  /// RP2350, 8 on the RP2040. The two directions have separate pools, so a
  /// burst towards the host cannot take the buffers that the host's
  /// acknowledgements need. Ignored on the ESP32.
  uint8_t framesFromHost = 0;

  /// Largest Ethernet frame the USB adapter announces to the host (the host
  /// derives its MTU from it).
  uint16_t usbMaxFrame = 1514;
};
