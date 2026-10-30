# Examples

The sketches are in the `examples` folder; open them from File -> Examples -> TinyWifiDongle.

| Example | Shows |
|---|---|
| `WifiDongle` | the minimum: hard-coded SSID and password |
| `WifiManager` | WiFi credentials entered in a browser with [WiFiManager](https://github.com/tzapu/WiFiManager) (Library Manager, tested with 2.0.17): the board opens the network `TinyWifiDongle-Setup` on first start, saves the choice in flash and hands the WiFi radio to the dongle afterwards. Press BOOT within 3 s after a start to forget the saved network. Compiles to about 80% of the flash |
