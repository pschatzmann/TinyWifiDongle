#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

namespace twd {

/**
 * @brief Captive portal in which the user picks a WiFi network and enters its password.
 *
 * run() scans for networks, opens an access point, answers every DNS query
 * with the board's own address (so a phone or laptop that joins the access
 * point opens the page by itself) and serves a small form until the user has
 * submitted a network. Built from the web server, DNS server and WiFi classes
 * that the ESP32 and the Raspberry Pi Pico cores both contain.
 */
class SetupPortal {
 public:
  /// The access point is called `network`; `password` (at least 8 characters) protects it, nullptr leaves it open.
  explicit SetupPortal(const char* network, const char* password = nullptr)
      : network_(network), apPassword_(password ? password : "") {}

  /// Prints what the portal does (address, connected devices, requests) to `out`; nullptr switches it off.
  void setLog(Print* out) { log_ = out; }

  /// Serves the form until a network is submitted and returns it. Blocks.
  void run(String& ssid, String& password) {
    submitted_ = false;
    scan();
    start();
    uint32_t lastReport = 0;
    int stations = -1;
    while (!submitted_) {
      dns_.processNextRequest();
      server_.handleClient();
      if (log_ && millis() - lastReport > 2000) {  // report when the number of connected devices changes
        lastReport = millis();
        int n = WiFi.softAPgetStationNum();
        if (n != stations) {
          stations = n;
          log_->printf("[setup] %d device(s) connected to the setup network\n", n);
        }
      }
      delay(2);
    }
    delay(1500);  // let the answer reach the browser
    stop();
    ssid = ssid_;
    password = enteredPassword_;
  }

 private:
  String network_, apPassword_;  // access point: name and password
  WebServer server_{80};
  DNSServer dns_;
  String options_;  // <option> list of the networks found by the scan
  String ssid_, enteredPassword_;  // what the user submitted
  bool submitted_ = false;
  Print* log_ = nullptr;

  static String escape(const String& s) {
    String out;
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else if (c == '&') out += "&amp;";
      else if (c == '"') out += "&quot;";
      else out += c;
    }
    return out;
  }

  // Scanning needs the station, so it happens before the access point opens.
  void scan() {
    options_ = "";
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    if (log_) log_->printf("[setup] scan found %d network(s)\n", n);
    for (int i = 0; i < n; i++) {
      String name = WiFi.SSID(i);
      if (log_) log_->printf("[setup]   '%s' channel %d, %d dBm\n", name.c_str(), (int)WiFi.channel(i), (int)WiFi.RSSI(i));
      if (name.length()) options_ += "<option value=\"" + escape(name) + "\">";
    }
  }

  void start() {
    WiFi.mode(WIFI_AP);
#if !defined(ESP32)
    // The Pico picks 192.168.4.1 or 192.168.42.1 depending on the state of the radio; fix it.
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
#endif
    if (apPassword_.length()) {
      WiFi.softAP(network_.c_str(), apPassword_.c_str());
    } else {
      WiFi.softAP(network_.c_str());
    }
    dns_.start(53, "*", WiFi.softAPIP());
    if (log_) log_->printf("[setup] setup network '%s' open, address %s\n", network_.c_str(), WiFi.softAPIP().toString().c_str());
    server_.on("/", [this]() {
      if (log_) log_->println("[setup] GET /");
      sendForm();
    });
    server_.on("/save", HTTP_POST, [this]() {
      if (log_) log_->println("[setup] POST /save");
      receive();
    });
    server_.onNotFound([this]() {  // connectivity checks of phones and laptops end up here
      if (log_) log_->printf("[setup] %s %s%s -> redirect\n", server_.method() == HTTP_GET ? "GET" : "other", server_.hostHeader().c_str(), server_.uri().c_str());
      server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
      server_.send(302, "text/plain", "");
    });
    server_.begin();
  }

  void stop() {
    server_.stop();
    dns_.stop();
    WiFi.softAPdisconnect(true);
    // Not WIFI_OFF: on the Raspberry Pi Pico that leaves the core in WIFI_AP, in which its begin() starts an
    // access point instead of joining a network.
    WiFi.mode(WIFI_STA);
  }

  void sendForm() {
    server_.send(200, "text/html",
                 String("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                        "<title>WiFi setup</title><h2>Choose your WiFi network</h2>"
                        "<form method=post action=/save>Network<br>"
                        "<input name=ssid list=networks required maxlength=32><datalist id=networks>") +
                     options_ +
                     "</datalist><br><br>Password<br><input name=password type=password maxlength=64>"
                     "<br><br><button>Save</button></form>");
  }

  void receive() {
    ssid_ = server_.arg("ssid");
    if (ssid_.length() == 0) {
      server_.send(400, "text/plain", "The network name is missing.");
      return;
    }
    enteredPassword_ = server_.arg("password");
    server_.send(200, "text/html",
                 "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<h2>Saved</h2>The board now joins the network. You can close this page.");
    submitted_ = true;
  }
};

}  // namespace twd
