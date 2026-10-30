#pragma once
#include <string.h>

#include "Interfaces.h"

namespace twd {

/**
 * @brief Layer-2 bridge between the USB host and the uplink network.
 *
 *     Host <-- UsbTransport <-- L2Bridge --> FrameLink --> access point
 *
 * Frames are forwarded unchanged in both directions: no IP stack, NAT or DHCP
 * server on the MCU, so the host gets its address from the uplink network's
 * own DHCP server. Two rules keep an access point happy:
 *  - the host adapter uses the link's MAC address (see UsbTransport::setHostMac),
 *    and frames from the host with any other source address are dropped;
 *  - frames from the link that carry that address as source are the access
 *    point echoing the host's own broadcasts, and are dropped.
 *
 * Shared by all ports; they only provide a UsbTransport and a FrameLink.
 */
class L2Bridge : public FrameReceiver {
 public:
  /// @brief Counters since start.
  struct Stats {
    uint32_t toLink;     ///< frames forwarded host -> uplink
    uint32_t toHost;     ///< frames forwarded uplink -> host
    uint32_t reflected;  ///< echoes of the host's own frames, dropped
    uint32_t foreign;    ///< host frames with a foreign source MAC, dropped
    uint32_t dropped;    ///< frames the other side could not take
  };

  /// Bridge `usb` and `link`.
  L2Bridge(UsbTransport& usb, FrameLink& link, Logger& log) : usb_(usb), link_(link), log_(log), down_(*this) {}

  /// Start forwarding; both sides must already be started.
  void begin() {
    usb_.setReceiver(this);
    link_.setFrameReceiver(&down_);
    log_.log("bridge: host adapter uses MAC %02x:%02x:%02x:%02x:%02x:%02x", link_.macAddress()[0],
             link_.macAddress()[1], link_.macAddress()[2], link_.macAddress()[3], link_.macAddress()[4],
             link_.macAddress()[5]);
  }

  /// Current counters.
  Stats stats() const { return stats_; }

  /// Host -> uplink (called by the UsbTransport).
  void onFrame(const uint8_t* frame, uint16_t len) override {
    if (len < 14) return;
    if (memcmp(frame + 6, link_.macAddress(), 6) != 0) {
      stats_.foreign++;
      return;
    }
    if (link_.sendFrame(frame, len)) {
      stats_.toLink++;
    } else {
      stats_.dropped++;
    }
  }

 private:
  /// Uplink -> host.
  class Downstream : public FrameReceiver {
   public:
    explicit Downstream(L2Bridge& bridge) : bridge_(bridge) {}
    void onFrame(const uint8_t* frame, uint16_t len) override { bridge_.fromLink(frame, len); }

   private:
    L2Bridge& bridge_;
  };

  void fromLink(const uint8_t* frame, uint16_t len) {
    if (len < 14) return;
    if (memcmp(frame + 6, link_.macAddress(), 6) == 0) {
      stats_.reflected++;
      return;
    }
    if (usb_.send(frame, len)) {
      stats_.toHost++;
    } else {
      stats_.dropped++;
    }
  }

  UsbTransport& usb_;
  FrameLink& link_;
  Logger& log_;
  Downstream down_;
  Stats stats_ = {};
};

}  // namespace twd
