#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_private/wifi.h"  // esp_wifi_internal_reg_rxcb / _tx / _free_rx_buffer

#include "../../core/Interfaces.h"

namespace twd {

/**
 * @brief Raw Ethernet access to the WiFi network through the ESP32's station.
 *
 * Joins the network and then takes over frame reception from the IP stack
 * (esp_wifi_internal_reg_rxcb), so everything the station receives is passed
 * to the FrameReceiver, and sends frames with esp_wifi_internal_tx(). The
 * station's own DHCP client is stopped: the MCU has no IP address, the host
 * behind the bridge uses the station's MAC address and asks the network
 * itself.
 *
 * Also an Uplink, so the composition can report the association state.
 * @see L2Bridge
 */
class Esp32WifiFrameLink : public Uplink, public FrameLink {
 public:
  /// Create the link; WiFi is not touched before begin().
  explicit Esp32WifiFrameLink(Logger& log) : log_(log) {}

  bool begin(const char* ssid, const char* password) override {
    instance() = this;
    esp_timer_create_args_t args = {};
    args.callback = &Esp32WifiFrameLink::claimRxTimer;
    args.arg = this;
    args.name = "wifiRx";
    if (esp_timer_create(&args, &claimTimer_) != ESP_OK) return false;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem sleep delays downlink packets; a dongle must stay awake
    stopDhcpClient();
    WiFi.onEvent(&Esp32WifiFrameLink::onConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(&Esp32WifiFrameLink::onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.begin(ssid, password);
    return true;
  }

  /// True while associated with the access point (the MCU has no IP address).
  bool connected() override { return associated_; }

  bool sendFrame(const uint8_t* frame, uint16_t len) override {
    if (!associated_) return false;
    return esp_wifi_internal_tx(WIFI_IF_STA, const_cast<uint8_t*>(frame), len) == ESP_OK;
  }

  void setFrameReceiver(FrameReceiver* receiver) override { receiver_ = receiver; }

  /// The station MAC as the WiFi stack reports it. On chips with their own
  /// radio this equals the MAC from the chip's fuses; on the ESP32-P4 the
  /// radio sits in a companion chip (ESP-Hosted), whose MAC is the one the
  /// access point sees. Starts the WiFi stack on the first call, so it must
  /// not be called from a time-critical path before begin(); the result is cached.
  const uint8_t* macAddress() override {
    if (!macValid_) {
      WiFi.mode(WIFI_STA);  // initializes WiFi (and the companion chip); harmless if already done
      if (esp_wifi_get_mac(WIFI_IF_STA, mac_) != ESP_OK) esp_read_mac(mac_, ESP_MAC_WIFI_STA);
      macValid_ = true;
    }
    return mac_;
  }

 private:
  static constexpr int kClaims = 4;  // times the receive hook is re-asserted after a connect

  Logger& log_;
  FrameReceiver* receiver_ = nullptr;
  esp_timer_handle_t claimTimer_ = nullptr;
  uint8_t mac_[6] = {};
  bool macValid_ = false;
  volatile bool associated_ = false;
  int claims_ = 0;

  static Esp32WifiFrameLink*& instance() {
    static Esp32WifiFrameLink* p = nullptr;
    return p;
  }

  static void stopDhcpClient() {
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) esp_netif_dhcpc_stop(sta);
  }

  /// WiFi -> host (WiFi task; must not block).
  static esp_err_t rxHook(void* buffer, uint16_t len, void* eb) {
    Esp32WifiFrameLink* s = instance();
    if (s && s->receiver_) s->receiver_->onFrame((const uint8_t*)buffer, len);
    esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
  }

  // The IP stack registers its own receive hook when the station connects,
  // possibly after our event handler ran. Register ours again a few times.
  void claimRx() {
    stopDhcpClient();
    esp_wifi_internal_reg_rxcb(WIFI_IF_STA, &Esp32WifiFrameLink::rxHook);
  }
  
  static void claimRxTimer(void* arg) {
    Esp32WifiFrameLink* s = (Esp32WifiFrameLink*)arg;
    s->claimRx();
    if (++s->claims_ < kClaims && s->associated_) esp_timer_start_once(s->claimTimer_, 250000);
  }

  static void onConnected(arduino_event_t*) {
    Esp32WifiFrameLink* s = instance();
    if (!s) return;
    s->associated_ = true;
    s->claims_ = 0;
    s->claimRx();
    esp_timer_stop(s->claimTimer_);
    esp_timer_start_once(s->claimTimer_, 250000);
    s->log_.log("WiFi associated, forwarding raw frames");
  }

  static void onDisconnected(arduino_event_t* e) {
    Esp32WifiFrameLink* s = instance();
    if (!s) return;
    s->associated_ = false;
    s->log_.log("WiFi disconnected, reason=%d", (int)e->event_info.wifi_sta_disconnected.reason);
  }
};

}  // namespace twd
