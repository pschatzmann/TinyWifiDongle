// Share the board's WiFi with a PC over USB, with the WiFi credentials entered
// in a browser instead of being hard-coded.
//
// Works on the ESP32-S3 / S2 and on the Raspberry Pi Pico W / Pico 2 W, and
// needs no additional library. Boards: see the WifiDongle example.
//
// First start (nothing saved yet):
//   1. The board opens the WiFi network "TinyWifiDongle-Setup".
//   2. Connect a phone or laptop to it; the setup page opens by itself
//      (otherwise browse to http://192.168.4.1).
//   3. Pick your WiFi network, enter the password, save.
// The credentials are stored in flash. At the next start the board joins your
// network directly, and from then on the USB adapter is bridged to it.
//
// To forget the saved network: press the BOOT button (ESP32) or the BOOTSEL
// button (Pico) within the first 3 seconds after a start. The network is also
// forgotten when the board cannot join it within 60 seconds of a start.
//
// The USB adapter does not exist while the setup runs. Messages go to Serial
// (see docs/logging.md). See docs/setup.md for the details and the settings.
#include "TinyWifiDongle.h"
#include "TinyWifiDongleSetup.h"

TinyWifiDongle dongle;
TinyWifiDongleSetup wifiSetup;

void setup() {
  Serial.begin(115200);
  TWD_LOGGER.begin(Serial);
  wifiSetup.begin();  // blocks until the board knows which network to join
  dongle.begin(wifiSetup.ssid(), wifiSetup.password());
}

void loop() {
  bool connected = dongle.isWifiConnected();
  wifiSetup.update(connected);
  delay(connected ? 1000 : 100);  // nothing to do once connected; look more often while joining
}
