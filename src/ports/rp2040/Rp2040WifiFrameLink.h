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
  /// While the station is not associated, a warning is logged this often.
  static constexpr uint32_t kNotJoinedReportMs = 10000;

  bool begin(const char* ssid, const char* password) override {
    instance() = this;
    macAddress();  // starts the chip if that has not happened yet
    // The core's begin() starts an access point when it finds the mode WIFI_AP, which a previous
    // setup page can leave behind (WiFi.mode(WIFI_OFF) does not reset it), so ask for the station.
    WiFi.mode(WIFI_STA);
    ssid_ = ssid;
    if (password) {
      WiFi.beginNoBlock(ssid, password);
    } else {
      WiFi.beginNoBlock(ssid);
    }
    timer_.user_data = this;
    timer_.do_work = &Rp2040WifiFrameLink::maintainCb;
    if (!async_context_add_at_time_worker_in_ms(__getEthernetContext(), &timer_, 500)) {
      TWD_LOGGER.error("WiFi: could not add the worker to the async context");
      return false;
    }
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
  FrameReceiver* receiver_ = nullptr;
  uint8_t mac_[6] = {};
  bool macValid_ = false;
  volatile bool connected_ = false;
  async_at_time_worker_t timer_ = {};
  String ssid_;
  uint32_t downSince_ = 0;   // millis() when the link went down, 0 = up
  uint32_t reportedAt_ = 0;  // millis() of the last "not associated" warning

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

  static const char* linkStatus() {
    switch (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA)) {
      case CYW43_LINK_JOIN: return "joined, link not up";
      case CYW43_LINK_FAIL: return "connection failed";
      case CYW43_LINK_NONET: return "network not found";
      case CYW43_LINK_BADAUTH: return "wrong password";
      default: return "joining";
    }
  }

  void maintain() {
    struct netif* n = __getCYW43Netif();
    if (!n) {
      if (connected_) TWD_LOGGER.warning("WiFi disconnected: the station interface is gone");
      connected_ = false;
      return;
    }
    if (n->input != &Rp2040WifiFrameLink::inputHook) {
      n->input = &Rp2040WifiFrameLink::inputHook;
      TWD_LOGGER.info("WiFi station: frames are bridged");
    }
    if (dhcp_supplied_address(n) || netif_dhcp_data(n)) dhcp_stop(n);  // the host asks the network, not us
    bool up = netif_is_link_up(n);
    if (up && !connected_) TWD_LOGGER.info("WiFi associated, forwarding raw frames");
    if (!up && connected_) TWD_LOGGER.warning("WiFi disconnected");
    uint32_t now = millis();
    if (up) {
      downSince_ = 0;
    } else if (!downSince_) {
      downSince_ = reportedAt_ = now | 1;
    } else if (now - reportedAt_ >= kNotJoinedReportMs) {  // the core retries silently: say why nothing happens
      TWD_LOGGER.warning("WiFi: not associated with '%s' for %u s (%s)", ssid_.c_str(),
                         (unsigned)((now - downSince_) / 1000), linkStatus());
      reportedAt_ = now;
    }
    connected_ = up;
  }
};

}  // namespace twd
