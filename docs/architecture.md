# Architecture

```
Host PC <-USB CDC-NCM-> UsbTransport <-> L2Bridge <-> FrameLink <-WiFi-> access point
```

Raw Ethernet frames are forwarded unchanged: no IP stack, NAT or DHCP server on the MCU. The host takes its address from the WiFi network's DHCP server. One class per concern; the parts only know each other through the interfaces in `src/core/Interfaces.h`.

| Class | Concern | Portable |
|---|---|---|
| `twd::LoggerClass` | printf-style logger, used through the global `TWD_LOGGER`; writes to a `Print` set by the sketch (discards until then) | yes |
| `twd::UsbTransport` | frames to/from the USB host | interface |
| `twd::FrameReceiver` | consumer of frames | interface |
| `twd::FrameLink` | raw Ethernet access to the WiFi network | interface |
| `twd::Uplink` | joining the WiFi network | interface |
| `twd::L2Bridge` | forwards frames between `UsbTransport` and `FrameLink` | yes |
| `twd::Esp32UsbNcm` | `UsbTransport` on TinyUSB CDC-NCM | ESP32 |
| `twd::Esp32UsbTaskPin` | runs the TinyUSB task on the interrupt's CPU core | ESP32 |
| `twd::Esp32WifiFrameLink` | `FrameLink` + `Uplink` on the WiFi station (also with a remote radio, ESP32-P4) | ESP32 |
| `twd::Esp32Dongle` | owns the parts, decides start order | ESP32 |
| `twd::Rp2040UsbNcm` | `UsbTransport` on the core's NCM class driver (queues + worker in the lwIP async context) | Raspberry Pi Pico |
| `twd::Rp2040WifiFrameLink` | `FrameLink` + `Uplink` on the CYW43 station (hooks the lwIP interface input) | Raspberry Pi Pico |
| `twd::Rp2040Dongle` | owns the parts, decides start order | Raspberry Pi Pico |
| `TinyWifiDongle` | public facade | yes |
| `TinyWifiDongleSetup` | finds the network to join: from flash or from a setup page ([setup.md](setup.md)) | yes (ESP32, Pico) |
| `twd::CredentialStore`, `twd::SetupPortal`, `twd::Board` | the parts of the setup: flash, captive portal, the board-specific button and restart | yes (ESP32, Pico) |

A new MCU needs `UsbTransport`, `FrameLink`/`Uplink` and a composition class; `L2Bridge`, `TWD_LOGGER` and the facade are reused. The Raspberry Pi Pico port shows a second way to do it: its WiFi chip is reached through the lwIP interface the core creates for it (input function replaced, `cyw43_send_ethernet()` to send); see [status.md](status.md). The task-pinning workaround below is ESP32-specific.

## Bridge rules
- The USB adapter uses the station's MAC address; the access point only accepts frames with that source address. Host frames with another source are dropped.
- Frames from the WiFi network that carry that address as source are the access point echoing the host's broadcasts, and are dropped.
- The station's own DHCP client is stopped and the station's receive path is taken over, so the board's IP stack sees no traffic:
  - ESP32: `esp_wifi_internal_reg_rxcb` is replaced; the IP stack registers its hook again on every connect, so the link re-asserts its own a few times afterwards. Frames are sent with `esp_wifi_internal_tx`. On the ESP32-P4 the radio is a companion chip (ESP-Hosted) and the functions are provided by esp_wifi_remote.
  - Raspberry Pi Pico: `netif->input` of the CYW43 station interface is replaced (the core delivers every received frame there); a worker re-applies it and stops DHCP twice a second. Frames are sent with `cyw43_send_ethernet`.

## ESP32 USB notes
- **Threading:** every call into the TinyUSB NCM driver runs in the TinyUSB task; other tasks enqueue frames and call `usbd_defer_func()`.
- **One frame per USB transfer**, padded so a transfer is never a multiple of 64 bytes (the zero-length-packet case wedges the driver).
- **Stall watchdog** (`Esp32UsbNcm::recover()`): drops the queued frames and re-enumerates USB (`tud_disconnect()`, 800 ms later `tud_connect()`) if sending makes no progress for `stallTimeoutMs` (default 1 s, `0` = off). The host sees a carrier change, so NetworkManager restarts the connection. It is a safety net: with `pinUsbTask` it triggers rarely.
- **Task pinning** (`Esp32UsbTaskPin`): see below.
- **Logging:** `TWD_LOGGER` writes directly to the `Print` the sketch set, from whatever task logs. With a USB CDC port that may arm its endpoint from the other CPU core than the USB interrupt, which races with the interrupt in the TinyUSB driver; a UART avoids this (see [logging.md](logging.md)). The core's own `log_x()` output is not redirected.
- **Diagnostics:** `Esp32UsbNcm::logControllerState()` prints the data endpoint's registers (`DIEPCTL`, `DIEPTSIZ`, `DIEPINT`, `DTXFSTS`, `DIEPEMPMSK`, `GINTSTS/GINTMSK`, `DAINT/DAINTMSK`); the stats task calls it instead of printing the counters while the send queue holds more than 8 frames.

## Raspberry Pi Pico notes
- __USB:__ the core's NCM class driver (`tusb-ncm`) with the USB stack "Pico SDK"; the interface is registered like the core's own NCM driver does it (`USB.registerInterface`, `USB.disconnect()`/`connect()` around it). `Rp2040UsbNcm` provides the `tud_network_*` callbacks.
- __Threading:__ the TinyUSB receive callback may run in interrupt context, so it only copies the frame into a buffer and queues it. A worker in the core's lwIP/CYW43 async context does the rest: it hands frames from the host to the bridge (which may call the WiFi chip there), pushes frames to the host into the USB stack, and calls `tud_network_recv_renew()`, all while holding the USB mutex (taken with try-enter, so it never blocks).
- __Buffers:__ two pools allocated by `begin()`, one per direction (`framesToHost`, `framesFromHost`; defaults 48 and 16 on the RP2350, 24 and 8 on the RP2040), each with a free list and a queue of `Frame*`. Separate pools keep a burst towards the host from taking the buffers that the host's acknowledgements need.
- __WiFi:__ association through the core's WiFi class; `netif->input` of the CYW43 station interface is replaced by a function that passes the frame to the bridge, DHCP on that interface is stopped, and frames are sent with `cyw43_send_ethernet()`. A worker re-applies the replacement and the DHCP stop twice a second.
- __Logging:__ `TWD_LOGGER` writes directly (no queue); see [logging.md](logging.md) for the limits of `Serial`.
- __No watchdog and no task pinning:__ the Pico port has neither, see [known-issues.md](known-issues.md).

## The USB send-path stall (ESP32)

### Symptom
Without the mitigation below, transfers to the host stop completing under sustained load, after a few hundred frames. The send queue fills up, the host receives nothing (DHCP and TCP stall), and the stall watchdog re-enumerates USB, which NetworkManager sees as a carrier change. Throughput is a few KB/s with regular reconnects.

### Diagnosis
`logControllerState()` shows the same state every time: the data IN endpoint is armed (`DIEPCTL.EPENA = 1`), `DIEPTSIZ` shows `XferSize = 0` with 5-23 packets (`PktCnt`) still owed, the TX FIFO is empty (`DTXFSTS` = all words free), `DIEPINT` shows "IN token received while the FIFO was empty", and `DIEPEMPMSK` is `0`: the per-endpoint gate of the "TX FIFO empty" interrupt is closed, so the driver is never asked to refill the FIFO. A bulk endpoint is only re-armed on completion, so it stays dead.

### Cause
The Arduino core ships TinyUSB 0.21.0 precompiled (arduino-esp32 3.3.12). Its `dwc2` driver for the ESP32-S2/S3 read-modify-writes shared registers (`DIEPEMPMSK` and others) from the USB interrupt and from the USB task without a common lock. The core creates the `usbd` task with `xTaskCreate`, i.e. without a core affinity, so task and interrupt can run on different CPU cores and overwrite each other's bits.

TinyUSB master fixes this (no release yet): `d2b6142b` (diepempmsk), `e42fa935` (GINTMSK/DAINTMSK), `ec7be1ed` (TX FIFO writes across cores), plus NCM `cf055c23` (carrier lost after a link-state collision). As of arduino-esp32 3.3.12 and 4.0.0-RC1, Espressif's TinyUSB 0.21.0.x and Adafruit TinyUSB 3.7.7, none of them contains these commits.

### Mitigation: `Esp32UsbTaskPin`
After `USB.begin()` the library waits until the core's `usbd` task is idle in its event wait, deletes it, and starts an identical task (`for (;;) tud_task();`) pinned to the core that started the USB stack. The USB interrupt is allocated on that core, so interrupt and task can no longer run in parallel. The stall watchdog (see above) recovers from what remains.

### Remaining stall
With the mitigation the stall still occurs, about once per 100-200 MB of traffic at 500-600 KB/s (ESP32-S3, 3 MB downloads). It also occurs without any periodic log output. The watchdog recovers it within a second or two at the price of a USB reconnect: open connections break and NetworkManager restarts DHCP. The stalled state is the one described under Diagnosis; the cause is not known. Ideas to investigate:

- activity on a second IN endpoint (the serial port) disturbing the data endpoint, through the shared `DIEPEMPMSK` word, the TX FIFO allocation or the interrupt handling; test by building without the serial port or by checking the TX FIFO sizes the driver gave each endpoint (`DIEPTXFn`);
- a further unlocked access in the driver, or a hardware limit of the 64-byte TX FIFO of the data endpoint.

### When to remove the workaround
When a core ships a TinyUSB that contains the upstream fixes above and the stall is gone without it: set `pinUsbTask = false` by default, or delete `Esp32UsbTaskPin`. Keep the watchdog as a safety net.
