#pragma once
#include <Arduino.h>
#include <LwipEthernet.h>  // __getEthernetContext()
#include <WiFi.h>
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <pico/async_context.h>
#include <pico/cyw43_arch.h>

#include "../../core/Interfaces.h"

// Defined by the core's lwIP_CYW43 library: the lwIP interface of the WiFi station.
struct netif* __getCYW43Netif();

namespace twd {

/**
 * @brief Raw Ethernet access to the WiFi network through the CYW43 chip of the Pico W / Pico 2 W.
 *
 * Joins the network with the core's WiFi class (which keeps its connect and
 * retry logic) and then takes over the station's lwIP interface: its input
 * function is replaced, so every frame the chip receives goes to the
 * FrameReceiver instead of the IP stack, and its DHCP client is stopped. Frames
 * are sent with cyw43_send_ethernet(). The board has no IP address of its own;
 * the host behind the bridge uses the station's MAC address and asks the
 * network itself.
 *
 * A worker in the core's lwIP async context re-applies these changes twice a
 * second, because the core may restart DHCP or reset the interface after a
 * reconnect.
 *
 * Also an Uplink, so the composition can report the association state.
 * @see L2Bridge
 */
class Rp2040WifiFrameLink : public Uplink, public FrameLink {
 public:
  /// Create the link; WiFi is not touched before macAddress() or begin().
  explicit Rp2040WifiFrameLink(Logger& log) : log_(log) {}

  bool begin(const char* ssid, const char* password) override {
    instance() = this;
    macAddress();  // starts the chip if that has not happened yet
    if (password) {
      WiFi.beginNoBlock(ssid, password);
    } else {
      WiFi.beginNoBlock(ssid);
    }
    timer_.user_data = this;
    timer_.do_work = &Rp2040WifiFrameLink::maintainCb;
    async_context_add_at_time_worker_in_ms(__getEthernetContext(), &timer_, 500);
    return true;
  }

  /// True while the station's link is up.
  bool connected() override { return connected_; }

  bool sendFrame(const uint8_t* frame, uint16_t len) override {
    if (!connected_) return false;
    return cyw43_send_ethernet(&cyw43_state, CYW43_ITF_STA, len, frame, false) == 0;
  }

  void setFrameReceiver(FrameReceiver* receiver) override { receiver_ = receiver; }

  /// The station MAC of the chip; starts the chip on the first call. The result is cached.
  const uint8_t* macAddress() override {
    if (!macValid_) {
      WiFi.macAddress(mac_);
      macValid_ = true;
    }
    return mac_;
  }

 private:
  Logger& log_;
  FrameReceiver* receiver_ = nullptr;
  uint8_t mac_[6] = {};
  bool macValid_ = false;
  volatile bool connected_ = false;
  async_at_time_worker_t timer_ = {};

  static Rp2040WifiFrameLink*& instance() {
    static Rp2040WifiFrameLink* p = nullptr;
    return p;
  }

  /// Replaces netif->input: chip -> bridge. Runs in the async context.
  static err_t inputHook(struct pbuf* p, struct netif*) {
    static uint8_t buffer[1600];  // one context, so one buffer is enough
    Rp2040WifiFrameLink* s = instance();
    if (s && s->receiver_ && p->tot_len <= sizeof(buffer)) {
      pbuf_copy_partial(p, buffer, p->tot_len, 0);
      s->receiver_->onFrame(buffer, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
  }

  static void maintainCb(async_context_t* ctx, async_at_time_worker_t* w) {
    ((Rp2040WifiFrameLink*)w->user_data)->maintain();
    w->next_time = make_timeout_time_ms(500);
    (void)ctx;
  }

  void maintain() {
    struct netif* n = __getCYW43Netif();
    if (!n) {
      connected_ = false;
      return;
    }
    if (n->input != &Rp2040WifiFrameLink::inputHook) {
      n->input = &Rp2040WifiFrameLink::inputHook;
      log_.log("WiFi station: frames are bridged");
    }
    if (dhcp_supplied_address(n) || netif_dhcp_data(n)) dhcp_stop(n);  // the host asks the network, not us
    bool up = netif_is_link_up(n);
    if (up != connected_) log_.log(up ? "WiFi associated, forwarding raw frames" : "WiFi disconnected");
    connected_ = up;
  }
};

}  // namespace twd
