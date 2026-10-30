# Status

| MCU | Status |
|---|---|
| ESP32-S3 / S2 | works: DHCP, ping, HTTP/HTTPS; sustained downloads at about 600 KB/s (5 Mbit/s), see Throughput below. **Rare USB stall remains** (about once per 100-200 MB, recovered by the watchdog within a second or two, see [known issue](known-issues.md)). Tested on an ESP32-S3 with Linux/NetworkManager only |
| ESP32-P4 | compiles; WiFi side tested on a Waveshare ESP32-P4-WIFI6; **USB side untested** (that board has no USB-OTG connector), see below |
| RP2040 W | planned, see [architecture.md](architecture.md) |

## ESP32-P4 (untested)
The P4 has a high-speed USB controller but no WiFi radio of its own: the core drives an ESP32-C6 companion chip over SDIO (ESP-Hosted / esp_wifi_remote). What is prepared:

- The USB adapter takes the station MAC reported by the WiFi stack (`esp_wifi_get_mac`), which on the P4 is the companion chip's MAC, the one the access point sees. On the ESP32-S3 it is the same value as before.
- Logging goes to UART0 (`Serial0`) by default on the P4, i.e. through the board's USB-UART bridge; the USB connection then carries only the network adapter.
- The register dump of the USB controller (`Esp32UsbNcm::logControllerState()`) exists on the ESP32-S2/S3 only; on other chips it only prints that it is not available.
- The raw WiFi functions the bridge uses (`esp_wifi_internal_reg_rxcb`, `esp_wifi_internal_tx`, `esp_wifi_internal_free_rx_buffer`) are provided by esp_wifi_remote.

**Tested** on a Waveshare ESP32-P4-WIFI6 (chip revision v3.2, build option `ChipVariant=postv3`): the board joins the WiFi network through the companion chip, and raw frames from the access point reach the bridge (the status line showed them as dropped, because no USB host was connected). Logging over `Serial0` through the board's USB-UART bridge works.

**Not tested:** that board has a single USB-C port, which is wired to the USB-UART bridge only; the P4's own USB-OTG lines are not available on a connector, so the USB adapter could not be tried. Still unknown: whether the adapter enumerates and bridges correctly, the send direction (PC to WiFi) through ESP-Hosted, the throughput of the SDIO link, and whether the USB stall (see [known-issues.md](known-issues.md)) occurs on the P4, whose USB controller may be driven differently. A P4 board with a USB-OTG connector is needed for this. Please report results, ideally with the status line (`statsIntervalMs = 1000`) of a failing run.

## Throughput
On the ESP32-S3, downloads through the dongle ran at 530-615 KB/s (about 5 Mbit/s), steadily, over more than 100 test downloads. For comparison, the PC's own WiFi in the same network gave 1.5-3.8 MB/s for a single stream and about 6.8 MB/s for four parallel streams (Cloudflare, 25 MB downloads). So the dongle is three to six times slower than a PC's WiFi.

The steady ceiling points to a limit in the dongle itself, not to WiFi fluctuation. The ESP32-S3 only has a full-speed USB controller (12 Mbit/s, about 1.1 MB/s for bulk data at best), so USB alone caps it well below the WiFi. The measured 0.6 MB/s is about half of that; where the other half goes (one frame per USB transfer with timer pacing in the send path, the WiFi throughput of the ESP32 itself) has not been measured. The ESP32-P4 has a high-speed USB controller (480 Mbit/s) and would remove the USB limit, but it is untested.
