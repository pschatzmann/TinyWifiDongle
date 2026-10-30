# Examples

The sketches are in the `examples` folder; open them from File -> Examples -> TinyWifiDongle.

| Example | Platform | Shows |
|---|---|---|
| `WifiDongle` | ESP32, Raspberry Pi Pico | the minimum: hard-coded SSID and password |
| `WifiManager` | ESP32, Raspberry Pi Pico | WiFi credentials entered in a browser: the board opens the network `TinyWifiDongle-Setup` on first start, saves the choice in flash and hands the WiFi chip to the dongle afterwards. Press BOOT (ESP32) / BOOTSEL (Pico) within 3 s after a start to forget the saved network. No additional library, see [setup.md](setup.md) |

Both sketches build for the ESP32-S3 and the Pico 2 W. `WifiDongle` has run on both; the setup page of `WifiManager` has not yet been run on a board.
