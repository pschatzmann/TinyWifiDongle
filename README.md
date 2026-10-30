# TinyWifiDongle

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)

A __header-only C++ Arduino library__ that turns a microcontroller into a __USB WiFi dongle__: the MCU joins a WiFi network and appears to your PC as a USB network adapter (CDC-NCM).

- No driver needed on the PC: Linux, macOS and Windows 11 have a CDC-NCM driver built in
- Ethernet frames are bridged unchanged between USB and WiFi, so the PC is a normal client of your WiFi network and gets its address from your router's DHCP server. The MCU itself has no IP address
- About 600 KB/s (5 Mbit/s) in the tests with the ESP32-S3, whose USB is full speed (12 Mbit/s): fine for browsing and downloads, but much slower than a PC's own WiFi ([details](docs/status.md))
- Log output on `Serial0`, or on a USB serial port next to the adapter that also lets you upload sketches without holding BOOT (ESP32-S2/S3)
- Credentials can be hard-coded or entered in a browser with [WiFiManager](docs/examples.md)
- Supported: ESP32-S3 / S2 ([status and known issue](docs/status.md)). ESP32-P4: compiles, WiFi side tested, USB side needs a board with a USB-OTG connector. Planned: RP2040 W

### Example

```cpp
#include "TinyWifiDongle.h"

TinyWifiDongle dongle;

void setup() {
  dongle.begin("ssid", "password");
}

void loop() {}
```

Select the board option __USB Mode: USB-OTG (TinyUSB)__ and connect the USB-OTG port of the board to the PC.

## Documentation

- [Examples](docs/examples.md)
- [Configuration](docs/configuration.md)
- [How it behaves](docs/behaviour.md) (addresses, serial port, status line, stall watchdog)
- [Host setup tips](docs/host-setup.md) (route priority, NetworkManager, Wake-on-LAN)
- [Status](docs/status.md) and [Known issue: the USB stall](docs/known-issues.md)
- [Architecture](docs/architecture.md) (classes, bridge rules, analysis of the USB stall)
- Class documentation: run `doxygen Doxyfile`, the HTML is generated into `docs/html-out/html/`

## Installation in Arduino

You can download the library as zip and call Sketch -> Include Library -> Add .ZIP Library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyWifiDongle.git
```

I recommend to use git because you can easily update to the latest version just by executing the ```git pull``` command in the project folder.

Requirements:

- ESP32 board package (arduino-esp32) 3.x, tested with 3.3.12. It must include the TinyUSB NCM class, which is on in the prebuilt libraries
- Board option __USB Mode: USB-OTG (TinyUSB)__, with the USB-OTG port of the board connected to the PC
- Include `TinyWifiDongle.h` from the sketch only, because the header defines the USB driver callbacks
- For the WiFiManager example: the library [WiFiManager](https://github.com/tzapu/WiFiManager) by tzapu (Library Manager)

Host support for CDC-NCM: Linux and macOS native, Windows 11 native (Windows 10 needs a driver).

## License

MIT, see [LICENSE](LICENSE).
