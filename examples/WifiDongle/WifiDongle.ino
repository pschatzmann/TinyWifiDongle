// Share the ESP32-S3's WiFi with a PC over USB.
//
// Board: ESP32-S3, "USB Mode" = "USB-OTG (TinyUSB)", "USB CDC On Boot" = disabled.
// Plug the USB-OTG port into the PC: a new network adapter appears. The adapter
// is bridged to the WiFi network below, so the PC gets its address from that
// network's DHCP server (e.g. 192.168.1.x) like any other WiFi client; the ESP32
// itself has no IP address.
//
// Besides the adapter the board shows up as a serial port, which lets you
// upload a new sketch without holding BOOT. See docs/configuration.md for the options.
#include "TinyWifiDongle.h"

TinyWifiDongle dongle;

void setup() {
  if (!dongle.begin("my-ssid", "my-password")) {
    while (true) delay(1000);
  }
}

void loop() {
  delay(1000);
}
