// Share the ESP32-S3's WiFi with a PC over USB, with the WiFi credentials
// entered in a browser instead of being hard-coded.
//
// Needs the library "WiFiManager" by tzapu (Library Manager).
// Board: ESP32-S3, "USB Mode" = "USB-OTG (TinyUSB)", "USB CDC On Boot" = disabled.
//
// First start (nothing saved yet):
//   1. The board opens the WiFi network "TinyWifiDongle-Setup".
//   2. Connect a phone or laptop to it; the setup page opens by itself
//      (otherwise browse to http://192.168.4.1).
//   3. Pick your WiFi network, enter the password, save.
// The credentials are stored in flash. At the next start the board joins your
// network directly, and from then on the USB adapter is bridged to it.
//
// To forget the saved network: press the BOOT button within the first 3 seconds
// after a start (do not hold it while resetting, that enters the bootloader).
//
// The setup page needs the ESP32 to have an IP address, which the bridge itself
// never has. That is why WiFiManager runs first and the dongle takes over the
// WiFi radio afterwards. The USB adapter does not exist while the setup runs.
#include <WiFiManager.h>

#include "TinyWifiDongle.h"

static const char* kSetupNetwork = "TinyWifiDongle-Setup";
static const int kForgetButton = 0;  // BOOT button of most ESP32-S3 boards

TinyWifiDongle dongle;

// True if the BOOT button is pressed during the first 3 seconds.
static bool forgetRequested() {
  pinMode(kForgetButton, INPUT_PULLUP);
  uint32_t start = millis();
  while (millis() - start < 3000) {
    if (digitalRead(kForgetButton) == LOW) return true;
    delay(10);
  }
  return false;
}

void setup() {
  WiFiManager wifiManager;
  if (forgetRequested()) wifiManager.resetSettings();

  // Joins the saved network, or opens the setup network and waits until
  // someone has entered working credentials.
  if (!wifiManager.autoConnect(kSetupNetwork)) {
    ESP.restart();
  }

  // Take over the credentials, then hand the WiFi radio to the dongle.
  String ssid = WiFi.SSID();
  String password = WiFi.psk();
  WiFi.disconnect(true);

  if (!dongle.begin(ssid.c_str(), password.length() ? password.c_str() : nullptr)) {
    while (true) delay(1000);
  }
}

void loop() {
  delay(1000);
}
