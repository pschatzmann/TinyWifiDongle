# Architecture

```
Host PC <-USB CDC-NCM-> UsbTransport <-> L2Bridge <-> FrameLink <-WiFi-> access point
```

Raw Ethernet frames are forwarded unchanged (as in DrWhax/esp32-usb-wifi): no IP stack, NAT or DHCP server on the MCU. The host takes its address from the WiFi network's DHCP server. One class per concern; the parts only know each other through the interfaces in `src/core/Interfaces.h`.

| Class | Concern | Portable |
|---|---|---|
| `twd::Logger` | log sink (discards by default) | yes |
| `twd::UsbTransport` | frames to/from the USB host | interface |
| `twd::FrameReceiver` | consumer of frames | interface |
| `twd::FrameLink` | raw Ethernet access to the WiFi network | interface |
| `twd::Uplink` | joining the WiFi network | interface |
| `twd::L2Bridge` | forwards frames between `UsbTransport` and `FrameLink` | yes |
| `twd::Esp32UsbNcm` | `UsbTransport` on TinyUSB CDC-NCM | ESP32 |
| `twd::Esp32UsbTaskPin` | runs the TinyUSB task on the interrupt's CPU core | ESP32 |
| `twd::Esp32WifiFrameLink` | `FrameLink` + `Uplink` on the WiFi station (also with a remote radio, ESP32-P4) | ESP32 |
| `twd::Esp32SerialLogger` | `Logger` on a USB CDC port; queues lines and writes them from the TinyUSB task | ESP32 |
| `twd::Esp32Dongle` | owns the parts, decides start order | ESP32 |
| `TinyWifiDongle` | public facade | yes |

A new MCU needs `UsbTransport`, `FrameLink`/`Uplink` and a composition class; `L2Bridge`, `Logger` and the facade are reused. For the RP2040 W: CDC-NCM on the Adafruit TinyUSB stack and the CYW43 chip's raw Ethernet access (`cyw43_send_ethernet` / `cyw43_cb_process_ethernet`). The task-pinning workaround below is ESP32-specific.

## Bridge rules
- The USB adapter uses the station's MAC address; the access point only accepts frames with that source address. Host frames with another source are dropped.
- Frames from the WiFi network that carry that address as source are the access point echoing the host's broadcasts, and are dropped.
- The station's own DHCP client is stopped and its receive hook (`esp_wifi_internal_reg_rxcb`) is taken over; the IP stack registers its hook again on every connect, so the link re-asserts its own a few times afterwards.

## ESP32 USB notes
- **Threading:** every call into the TinyUSB NCM driver runs in the TinyUSB task; other tasks enqueue frames and call `usbd_defer_func()`.
- **One frame per USB transfer**, padded so a transfer is never a multiple of 64 bytes (the zero-length-packet case wedges the driver).
- **Stall watchdog** (`Esp32UsbNcm::recover()`): drops the queued frames and re-enumerates USB (`tud_disconnect()`, 800 ms later `tud_connect()`) if sending makes no progress for `stallTimeoutMs` (default 1 s, `0` = off). The host sees a carrier change, so NetworkManager restarts the connection. It is a safety net: with `pinUsbTask` it triggers rarely.
- **Task pinning** (`Esp32UsbTaskPin`): see below.
- **Logging:** `log()` may be called from any task, but the text is queued (16 lines) and written from the TinyUSB task, so the CDC endpoint is armed on the USB interrupt's core. The core's own `log_x()` output is not redirected to the port (that path writes from arbitrary cores).
- **Diagnostics:** `Esp32UsbNcm::logControllerState()` prints the data endpoint's registers (`DIEPCTL`, `DIEPTSIZ`, `DIEPINT`, `DTXFSTS`, `DIEPEMPMSK`, `GINTSTS/GINTMSK`, `DAINT/DAINTMSK`); the stats task calls it instead of printing the counters while the send queue holds more than 8 frames.

## The USB send-path stall

### Symptom
Under sustained load the MCU -> host transfers stopped completing after a few hundred frames. The send queue filled up, the busy counter ran, the host got no data (so DHCP and TCP died), and the stall watchdog (set to 3 s at the time) re-enumerated USB about every 10 s while traffic ran (3 s stall + reconnect + DHCP; each followed by `carrier-changed` in NetworkManager). Throughput was a few KB/s.

### Stalled controller state
Always the same, read with `logControllerState()`: the data IN endpoint is armed (`DIEPCTL.EPENA = 1`), `DIEPTSIZ` shows `XferSize = 0` with 5-23 packets (`PktCnt`) still owed, the TX FIFO is empty (`DTXFSTS` = all words free), `DIEPINT` shows "IN token received while the FIFO was empty", and `DIEPEMPMSK` is `0`: the per-endpoint gate of the "TX FIFO empty" interrupt is closed, so the driver is never asked to refill the FIFO. A bulk endpoint is only re-armed on completion, so it stays dead.

### Cause
The Arduino core 3.3.12 ships TinyUSB 0.21.0 precompiled. Its `dwc2` driver for the ESP32-S2/S3 read-modify-writes shared registers (`DIEPEMPMSK` and others) from the USB interrupt and from the USB task without a common lock. The core creates the `usbd` task with `xTaskCreate`, i.e. without a core affinity, so the task and the interrupt can run on different CPU cores and overwrite each other's bits. Upstream fixed this in TinyUSB master (no release yet): `d2b6142b` (diepempmsk), `e42fa935` (GINTMSK/DAINTMSK), `ec7be1ed` (TX FIFO writes across cores), plus NCM `cf055c23` (carrier lost after a link-state collision). They are not in arduino-esp32 3.3.12, 4.0.0-alpha1/RC1 (Espressif's TinyUSB 0.21.0.x still has the unlocked clear), or Adafruit TinyUSB 3.7.7.

### Mitigation: `Esp32UsbTaskPin`
After `USB.begin()` the library waits until the core's `usbd` task is idle in its event wait, deletes it, and starts an identical task (`for (;;) tud_task();`) pinned to the core that started the USB stack. The USB interrupt is allocated on that core, so interrupt and task can no longer run in parallel. This makes the stall rare but does not remove it (see the table).

### What was tried (same load test through the dongle, Cloudflare 3 MB downloads)
| Configuration | Result |
|---|---|
| core as shipped | stalls within a few hundred frames, ~7 KB/s |
| NAT router mode instead of bridge | same stall (so not the bridge / lwIP) |
| no log traffic, MTU 576 or 1400, no zero-length-packet case | same stall |
| driver patched with the three upstream dwc2 commits | first download 603 KB/s, then stall; with the task pinned in addition: about 55 MB through, then stall (so the patch added nothing measurable and was removed) |
| task pinned, core's original driver, status line logged every 1 s straight from the stats task | 43 downloads (about 129 MB, in two runs of 25 and 18) at 500-615 KB/s, then a stall that stayed (watchdog off) |
| task pinned, status line logged every 1 s, queued and written from the USB task | 15 downloads (45 MB), then a stall that stayed (one run, started after a PC restart; the first ping showed 1.2 s round trip time) |
| task pinned, no periodic logging (`statsIntervalMs = 0`) | 60 of 60 downloads (180 MB), 327-613 KB/s (average 533), no stall, watchdog off |
| **library defaults: pinned, no periodic logging, watchdog 1 s** | **59 of 60 downloads (177 MB delivered) at 529-613 KB/s (average 593); a stall at download #44 (about 132 MB) was recovered by the watchdog within a second or two (one USB re-enumeration), downloads #45-#60 passed** |

Also checked and ruled out: USB autosuspend (`power/control = on`, device stays active), Wake-on-LAN (no support in driver or device), WiFi modem sleep (switched off by the library), path MTU of the test network.

Not usable as a fix: arduino-esp32 4.0.0-alpha1/RC1 (no TinyUSB fix, some components missing), Adafruit TinyUSB 3.7.7 (older than the fix), TinyPPP (PPP over USB serial uses the same IN-endpoint driver and needs an IP stack again).

### Open: the remaining stall
Same stalled state as before (armed transfer, `XferSize = 0`, packets owed, empty FIFO, `DIEPEMPMSK = 0`), but now only after tens to hundreds of MB instead of a few hundred frames.

**Observation:** the stall comes after a large, varying amount of traffic: 45 MB and 129 MB with a status line every second, 132 MB without periodic logging (library defaults), and not at all in one 180 MB run without periodic logging. So periodic serial output is **not required** for the stall; whether it makes it more likely cannot be told from these few runs (one or two per variant).

**Consequence for the hypotheses:**
1. *Another core arms the CDC endpoint* (the scenario of upstream commit `d2b6142b`): argued against by the runs above, because queuing the writes onto the USB core did not remove the stall and a stall also happened without periodic logging.
2. *Activity on a second IN endpoint disturbs the data endpoint* (shared `DIEPEMPMSK` word, TX FIFO allocation, interrupt handling in the driver): not tested. Test: remove the CDC interface completely (flashing then needs BOOT+RESET), or check the TX FIFO sizes/positions the driver gave the two endpoints (`DIEPTXFn`).
3. *A further unlocked access in the driver*, or a hardware limit of the 64-byte TX FIFO of the data endpoint: not tested.

**Practical consequence:** with the library defaults the stall is rare (once per roughly 100-200 MB in the tests) and the watchdog recovers it in a second or two, at the price of a USB reconnect (open connections break, NetworkManager restarts DHCP). `statsIntervalMs` defaults to `0` because the status line is only useful while debugging; it has not been shown to cause the stall. Further work should find the cause of the remaining stall (hypotheses 2 and 3).

### When to remove the workaround
When a core ships a TinyUSB that contains the upstream fixes above and the stall is gone without it: set `pinUsbTask = false` by default, or delete `Esp32UsbTaskPin`. Keep the watchdog as a safety net.
