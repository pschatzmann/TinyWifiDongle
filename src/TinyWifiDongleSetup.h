#pragma once
/**
 * @file TinyWifiDongleSetup.h
 * @brief Lets the user enter the WiFi credentials in a browser instead of hard-coding them.
 *
 * Separate from TinyWifiDongle.h, so sketches that do not need it do not pull
 * in the web server. Works on the ESP32 and on the Raspberry Pi Pico W / Pico 2 W
 * and needs no additional library.
 */
#include "setup/Board.h"
#include "setup/CredentialStore.h"
#include "setup/SetupPortal.h"

/**
 * @brief Finds out which WiFi network to join: from flash, or from the user through a setup page.
 *
 * begin() uses the network saved in flash. If there is none, the board opens
 * its own WiFi network (the "setup network"); a phone or laptop that joins it
 * is led to a page on which the user picks a network and enters the password.
 * The choice is saved in flash, so the next start needs no setup.
 *
 * The setup page needs the board to have an IP address, which the bridge of
 * TinyWifiDongle never has. So the setup runs first and the dongle takes over
 * the WiFi chip afterwards; the USB adapter does not exist while the setup runs.
 *
 * @code
 * TinyWifiDongle dongle;
 * TinyWifiDongleSetup wifiSetup;
 * void setup() {
 *   wifiSetup.begin();
 *   dongle.begin(wifiSetup.ssid(), wifiSetup.password());
 * }
 * void loop() {
 *   bool connected = dongle.isWifiConnected();
 *   wifiSetup.update(connected);
 *   delay(connected ? 1000 : 100);
 * }
 * @endcode
 */
class TinyWifiDongleSetup {
 public:
  /// The setup network is called `setupNetwork`; `setupPassword` (at least 8 characters) protects it, nullptr leaves it open.
  explicit TinyWifiDongleSetup(const char* setupNetwork = "TinyWifiDongle-Setup", const char* setupPassword = nullptr)
      : portal_(setupNetwork, setupPassword) {}

  /// How long, after a start, the BOOT button (ESP32) or BOOTSEL button (Pico) forgets the saved network; 0 switches the button off. Default 3000.
  void setForgetWindow(uint32_t ms) { forgetWindowMs_ = ms; }

  /// How long a start may take to join the network before the saved network is forgotten and the setup opens again; 0 = never. Default 60000.
  void setJoinTimeout(uint32_t ms) { joinTimeoutMs_ = ms; }

  /// Prints what the setup does (the address of the setup page, connected devices, requests) to `out`, e.g. `Serial`; nullptr switches it off.
  void setLog(Print* out) { portal_.setLog(out); }

  /// Loads the saved network, or opens the setup and blocks until the user has entered one.
  void begin() {
    bool known = store_.load();
    if (forgetWindowMs_ && twd::Board::buttonPressedWithin(forgetWindowMs_)) {
      store_.forget();
      known = false;
    }
    if (!known) {
      String ssid, password;
      portal_.run(ssid, password);
      store_.save(ssid.c_str(), password.c_str());
    }
    startedAt_ = millis();
  }

  /// Name of the network to join; valid after begin().
  const char* ssid() const { return store_.ssid(); }

  /// Password of the network to join, nullptr for an open network; valid after begin().
  const char* password() const { return store_.password(); }

  /// Removes the saved network; the next start opens the setup.
  void forget() { store_.forget(); }

  /// Call regularly with TinyWifiDongle::isWifiConnected(). If the board has not joined the network within the join timeout of a start, the saved network is forgotten and the board restarts, so that the setup opens again.
  void update(bool connected) {
    if (connected) {
      connected_ = true;
    } else if (!connected_ && joinTimeoutMs_ && millis() - startedAt_ > joinTimeoutMs_) {
      store_.forget();
      twd::Board::restart();
    }
  }

 private:
  twd::CredentialStore store_;
  twd::SetupPortal portal_;
  uint32_t forgetWindowMs_ = 3000;
  uint32_t joinTimeoutMs_ = 60000;
  uint32_t startedAt_ = 0;
  bool connected_ = false;
};
