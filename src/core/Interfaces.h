#pragma once
/**
 * @file Interfaces.h
 * @brief Portable abstractions the dongle is built from.
 *
 *   Host <--USB--> UsbTransport <--> L2Bridge <--> FrameLink <--WiFi--> access point
 *
 * Every MCU port provides implementations of UsbTransport, FrameLink and
 * Uplink (and optionally a Logger); the classes in core/ only depend on these
 * interfaces.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#ifndef TWD_PRINTF_FORMAT
#define TWD_PRINTF_FORMAT(fmt, args) __attribute__((format(printf, fmt, args)))
#endif

namespace twd {

/**
 * @brief Printf-style logging sink.
 *
 * The base class discards everything, so components can always log without
 * checking whether logging is wanted. Ports derive from it to write somewhere
 * useful (e.g. a USB serial port) and override enabled() and write().
 */
class Logger {
 public:
  virtual ~Logger() = default;

  /// Formats and writes one line; does nothing if the logger is not enabled.
  void log(const char* fmt, ...) TWD_PRINTF_FORMAT(2, 3) {
    if (!enabled()) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(buf);
  }

  /// True if log() output is written somewhere.
  virtual bool enabled() { return false; }

 protected:
  /// Writes one finished line; override in derived classes.
  virtual void write(const char* line) { (void)line; }
};

/**
 * @brief Consumer of Ethernet frames.
 *
 * Implemented by the network interface; a UsbTransport hands every frame it
 * receives from the host to its FrameReceiver.
 */
class FrameReceiver {
 public:
  virtual ~FrameReceiver() = default;
  /// The frame is only valid during the call; copy it if it must be kept.
  virtual void onFrame(const uint8_t* frame, uint16_t len) = 0;
};

/**
 * @brief Moves Ethernet frames between the MCU and the USB host.
 *
 * Hides the USB network class (CDC-NCM, ECM, ...) and the USB stack of the
 * MCU behind four operations: register, query, send and receive.
 * @see FrameReceiver
 */
class UsbTransport {
 public:
  virtual ~UsbTransport() = default;
  /// Registers the USB network interface (before the USB stack is started).
  virtual bool begin(const char* productName) = 0;
  /// The host has enumerated and configured the device.
  virtual bool ready() = 0;
  /// Queues a frame for the host. Never blocks; false if it was dropped.
  virtual bool send(const uint8_t* frame, uint16_t len) = 0;
  /// Frames coming from the host are passed to `receiver`.
  virtual void setReceiver(FrameReceiver* receiver) = 0;
  /// MAC address of the adapter as the host sees it (default: derived from the
  /// chip id). Call before begin().
  virtual void setHostMac(const uint8_t mac[6]) = 0;
};

/**
 * @brief The connection to the WiFi network the traffic is bridged to.
 *
 * Only association is modelled: the MCU takes no part in IP, so it has no
 * address, gateway or DNS server of its own.
 */
class Uplink {
 public:
  virtual ~Uplink() = default;
  /// Start joining the network; returns at once, see connected().
  virtual bool begin(const char* ssid, const char* password) = 0;
  /// True while associated with the access point.
  virtual bool connected() = 0;
};

/**
 * @brief Raw Ethernet access to the uplink network (bridge mode).
 *
 * Frames are exchanged as they are on the wire; the MCU takes no part in
 * layer 3. The access point only accepts frames that carry macAddress() as
 * source address.
 * @see L2Bridge
 */
class FrameLink {
 public:
  virtual ~FrameLink() = default;
  /// Sends a frame to the uplink network; false if it was dropped.
  virtual bool sendFrame(const uint8_t* frame, uint16_t len) = 0;
  /// Frames coming from the uplink network are passed to `receiver`.
  virtual void setFrameReceiver(FrameReceiver* receiver) = 0;
  /// MAC address the uplink network knows this device by.
  virtual const uint8_t* macAddress() = 0;
};

}  // namespace twd
