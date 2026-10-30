# Status

| MCU | Status |
|---|---|
| ESP32-S3 / S2 | works: DHCP, ping, HTTP/HTTPS; sustained downloads at about 600 KB/s (5 Mbit/s), see Throughput below. **Rare USB stall remains** (about once per 100-200 MB, recovered by the watchdog within a second or two, see [known issue](known-issues.md)). Tested on an ESP32-S3 with Linux/NetworkManager only |
| ESP32-P4 | compiles; WiFi side tested on a Waveshare ESP32-P4-WIFI6; **USB side untested** (that board has no USB-OTG connector), see below |
| Raspberry Pi Pico 2 W (RP2350 + CYW43) | works: the adapter enumerates, DHCP from the router, ping, HTTP/HTTPS, downloads at about 250 KB/s. Tested on a Pico 2 W with arduino-pico 6.3.1 and Linux/NetworkManager only, see Raspberry Pi Pico below. One unexplained reboot seen in long tests, see [known-issues.md](known-issues.md) |
| Raspberry Pi Pico W (RP2040) | same code and WiFi chip; not tested |

## ESP32-P4 (untested)
The P4 has a high-speed USB controller but no WiFi radio of its own: the core drives an ESP32-C6 companion chip over SDIO (ESP-Hosted / esp_wifi_remote). What is prepared:

- The USB adapter takes the station MAC reported by the WiFi stack (`esp_wifi_get_mac`), which on the P4 is the companion chip's MAC, the one the access point sees. On the ESP32-S3 it is the same value as before.
- Log to UART0 (`TWD_LOGGER.begin(Serial0)`) on the P4, i.e. through the board's USB-UART bridge; the USB connection then carries only the network adapter.
- The register dump of the USB controller (`Esp32UsbNcm::logControllerState()`) exists on the ESP32-S2/S3 only; on other chips it only prints that it is not available.
- The raw WiFi functions the bridge uses (`esp_wifi_internal_reg_rxcb`, `esp_wifi_internal_tx`, `esp_wifi_internal_free_rx_buffer`) are provided by esp_wifi_remote.

**Tested** on a Waveshare ESP32-P4-WIFI6 (chip revision v3.2, build option `ChipVariant=postv3`): the board joins the WiFi network through the companion chip, and raw frames from the access point reach the bridge (the status line showed them as dropped, because no USB host was connected). Logging over `Serial0` through the board's USB-UART bridge works.

**Not tested:** that board has a single USB-C port, which is wired to the USB-UART bridge only; the P4's own USB-OTG lines are not available on a connector, so the USB adapter could not be tried. Still unknown: whether the adapter enumerates and bridges correctly, the send direction (PC to WiFi) through ESP-Hosted, the throughput of the SDIO link, and whether the USB stall (see [known-issues.md](known-issues.md)) occurs on the P4, whose USB controller may be driven differently. A P4 board with a USB-OTG connector is needed for this. Please report results, ideally with the status line (`statsIntervalMs = 1000`) of a failing run.

## Throughput
Downloads through the dongle (Cloudflare, 3 MB each):

| Board | Speed |
|---|---|
| ESP32-S3 | 530-615 KB/s (about 5 Mbit/s), steady |
| Raspberry Pi Pico 2 W | roughly 110-325 KB/s, average about 250, varying |
| for comparison: the PC's own WiFi in the same network | 1.5-3.8 MB/s for one stream, about 6.8 MB/s for four parallel streams |

So the dongle is several times slower than a PC's WiFi. Both chips have only a full-speed USB controller (12 Mbit/s, about 1.1 MB/s for bulk data at best), which alone caps the dongle well below the WiFi. The measured rates are about half of that on the ESP32-S3 and lower on the Pico; the other limits (one frame per USB transfer with timer pacing in the send path, the WiFi throughput of the chip) are not measured. The ESP32-P4 has a high-speed USB controller (480 Mbit/s) and would remove the USB limit, but its USB side is untested.

## Raspberry Pi Pico (W / 2 W)
Port for the arduino-pico core (`rp2040:rp2040`, tested with 6.3.1) with the default option __USB Stack: Pico SDK__. How it works:

- USB: the core's CDC-NCM class driver (`tusb-ncm`), registered like the core's own NCM driver does it. Frames are exchanged through queues and two pools of frame buffers, one per direction (`framesToHost`, `framesFromHost`, see [configuration.md](configuration.md)); a worker in the core's lwIP/CYW43 async context moves them in and out of the USB stack under the USB mutex.
- WiFi: the connection is made with the core's WiFi class. Afterwards the station's lwIP interface gets a replaced input function (frames go to the bridge) and its DHCP client is stopped; frames are sent with `cyw43_send_ethernet()`. A worker re-applies this twice a second, because the core may restart DHCP after a reconnect. The adapter uses the chip's station MAC address.
- Logging: to the USB serial port (`TWD_LOGGER.begin(Serial)`) or to `Serial1` (GP0/GP1).
- Ignored on the Pico: `manageUsb`, `pinUsbTask`, `stallTimeoutMs`, `usbMaxFrame` (they belong to the ESP32 USB driver).

**Tested** on a Pico 2 W (MTU 1400 on the host): the adapter appears next to a serial port, the host gets its address from the router (192.168.1.x) through the bridge, ping to the router takes about 5 ms, HTTPS requests under 0.2 s. With the default buffers, 40 consecutive 3 MB downloads (120 MB) all completed, at 110-320 KB/s (average 246 KB/s); about 1.5% of the frames towards the host were dropped because a buffer pool was full, and none of the frames from the host.

The speed is about half of the ESP32-S3's. Like the ESP32-S3, the RP2350 has only a full-speed USB controller (12 Mbit/s). Dropped frames slow TCP down, so more buffers for frames to the host (`framesToHost`) may help; other candidates are the frame handling in the async context, the small NTB buffers of the core's USB configuration and the WiFi chip's SPI link. These are not measured.

## WiFi setup page
`TinyWifiDongleSetup` (see [setup.md](setup.md)) builds for the ESP32-S3 and the Pico 2 W. On a Pico 2 W the whole flow was run: setup network, web form, saving, joining the network (the adapter received an address from the router) and joining again after a replug. On the ESP32-S3 the setup page has not yet been run on a board.
