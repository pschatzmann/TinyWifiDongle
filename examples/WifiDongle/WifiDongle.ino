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
// Messages go to Serial: UART0 on the ESP32-S3 / S2, the USB serial port on
// the Pico. Remove TWD_LOGGER.begin() for no output. See the wiki page Logging and
// Configuration for the options.
#include "TinyWifiDongle.h"

TinyWifiDongle dongle;

void setup() {
  Serial.begin(115200);
  TWD_LOGGER.begin(Serial);  // before dongle.begin(), so its messages are not lost
  if (!dongle.begin("my-ssid", "my-password")) {
    while (true) delay(1000);
  }
}

void loop() {
  delay(1000);
}
