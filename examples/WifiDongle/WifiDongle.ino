// Share the board's WiFi with a PC over USB.
//
// Boards:
//   ESP32-S3 / S2: option "USB Mode" = "USB-OTG (TinyUSB)", "USB CDC On Boot" = disabled.
//                  Connect the USB-OTG port to the PC.
//   Raspberry Pi Pico W / Pico 2 W: keep the default "USB Stack" = "Pico SDK".
//                  Connect the USB connector to the PC.
//
// A new network adapter appears on the PC. It is bridged to the WiFi network
// below, so the PC gets its address from that network's DHCP server (e.g.
// 192.168.1.x) like any other WiFi client; the board itself has no IP address.
//
// Besides the adapter the board shows up as a serial port, which lets you
// upload a new sketch without holding BOOT/BOOTSEL. See docs/configuration.md
// for the options.
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
