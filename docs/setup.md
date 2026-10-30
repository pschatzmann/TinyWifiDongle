# WiFi setup in a browser

`TinyWifiDongleSetup` (header `TinyWifiDongleSetup.h`) lets the user enter the WiFi name and password in a browser, so a sketch does not need them hard-coded. It works on the ESP32 and on the Raspberry Pi Pico W / Pico 2 W and needs no additional library; it only uses the web server, DNS server, WiFi and EEPROM classes that both Arduino cores contain. It is a separate header, so sketches that do not include it do not pull in the web server.

```cpp
#include "TinyWifiDongle.h"
#include "TinyWifiDongleSetup.h"

TinyWifiDongle dongle;
TinyWifiDongleSetup wifiSetup;

void setup() {
  wifiSetup.begin();  // blocks until the board knows which network to join
  dongle.begin(wifiSetup.ssid(), wifiSetup.password());
}

void loop() {
  bool connected = dongle.isWifiConnected();
  wifiSetup.update(connected);
  delay(connected ? 1000 : 100);  // nothing to do once connected; look more often while joining
}
```

__Status:__ run on a Pico 2 W (setup page, saving, joining, and joining again after a replug); builds for the ESP32-S3, where the setup page itself has not yet been run on a board.

## How it works
1. `begin()` reads the network saved in flash.
2. If there is none, the board scans for networks, then opens a WiFi network of its own, the __setup network__ (`TinyWifiDongle-Setup`, open by default). Every DNS query is answered with the board's address, so a phone or laptop that joins the setup network opens the page by itself. Otherwise browse to `http://192.168.4.1` (the address of the board on the setup network, on both platforms). Some phones, Android in particular, do not offer to "sign in" to a network without internet and send browser traffic over mobile data instead; then switch mobile data off while doing the setup and open the address by hand. `setLog(&Serial)` prints the address and every request, which shows what the phone does.
3. The page offers the networks found by the scan (the name can also be typed) and asks for the password. After saving, the setup network closes and `begin()` returns.
4. The choice is kept in flash (the EEPROM emulation of the core, no filesystem partition needed), so the next start joins directly.

The setup page needs the board to have an IP address, which the bridge of `TinyWifiDongle` never has. So the setup runs first, and the dongle takes over the WiFi chip afterwards. The USB adapter does not exist while the setup runs, and neither does a USB serial port: the board is invisible to the PC. To upload a new sketch to a board that is in the setup, put it into its bootloader by hand: hold BOOTSEL while plugging in the USB cable (Pico), or use BOOT+RESET (ESP32).

## Forgetting the network
- Press the BOOT button (ESP32) or the BOOTSEL button (Pico) within the first 3 seconds after a start.
- If the board has not joined the network within 60 seconds of a start (`update()` sees `isWifiConnected()` stay false), the saved network is forgotten and the board restarts, so a wrong password cannot lock you out. Once the board has joined once, a later loss of the network does not forget it.
- `forget()` removes it from the sketch.

## Settings (class `TinyWifiDongleSetup`)
| Method | Meaning |
|---|---|
| constructor `(setupNetwork, setupPassword)` | name of the setup network (default `"TinyWifiDongle-Setup"`) and its password (at least 8 characters; `nullptr` = open) |
| `setForgetWindow(ms)` | how long after a start the button forgets the network; `0` switches the button off (default 3000) |
| `setJoinTimeout(ms)` | how long a start may take to join before the network is forgotten; `0` = never (default 60000) |
| `begin()` | loads the saved network or runs the setup; blocks |
| `ssid()`, `password()` | the network to join; `password()` is `nullptr` for an open network |
| `forget()` | removes the saved network |
| `update(connected)` | call from `loop()` with `dongle.isWifiConnected()` |

## Classes
`TinyWifiDongleSetup` is built from three small classes in `src/setup/`: `twd::CredentialStore` (the flash), `twd::SetupPortal` (scan, setup network, DNS, web page) and `twd::Board` (the BOOT/BOOTSEL button and the restart, the only code that differs between the boards).
