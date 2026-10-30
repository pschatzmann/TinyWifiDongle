# Known issues

## The USB stall (ESP32)

__Platform:__ ESP32-S3 (observed), probably ESP32-S2. Not seen on the Raspberry Pi Pico, which uses a different USB stack.

The Arduino core's TinyUSB (0.21.0) has a race between its USB task and the USB interrupt when they run on different CPU cores. A bulk IN transfer then loses its "TX FIFO empty" interrupt and never completes, and the network send path stalls.

- __Without mitigation:__ sustained transfers stall after a few hundred frames, giving a few KB/s with regular reconnects.
- __With `pinUsbTask` (default):__ the USB task runs on the core of the USB interrupt. The stall becomes rare, about once per 100-200 MB at 500-600 KB/s.
- __Recovery:__ the stall watchdog (`stallTimeoutMs`, default 1 s) re-enumerates USB. A stall then costs a second or two and a USB reconnect: open connections break and NetworkManager restarts DHCP.

The cause of the remaining stalls is not known. Diagnosis, the upstream fixes and the open questions are in [architecture.md](architecture.md).

## Raspberry Pi Pico

__Platform:__ Pico 2 W (tested). The Pico W has the same code and WiFi chip.

- __Unexplained reboot:__ in one long test the Pico rebooted once, about 10 minutes and 147 MB into continuous downloads. The host saw it as a USB disconnect followed by a second enumeration (first the serial port, then the network adapter), and NetworkManager did not bring the adapter back up by itself. The cause is not known. It did not recur in two later tests of 229 MB and 120 MB, in which the free heap stayed constant. There is no watchdog on the Pico port.
- __Dropped frames under load:__ when a buffer pool is full, frames are dropped; the counters `td` (to the host) and `rd` (from the host) of the status line show it. A few drops are normal, because TCP slows down, but many slow the transfer. Raise `framesToHost` or `framesFromHost` (see [configuration.md](configuration.md)) if the counters grow fast.
- __Log output:__ see [behaviour.md](behaviour.md).
