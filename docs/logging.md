# Logging

The library writes its messages to the global logger `TWD_LOGGER` (class `twd::LoggerClass`, [src/core/Logger.h](../src/core/Logger.h)). It prints nothing until the sketch gives it an output, so a sketch without logging pays nothing but the calls.

## Setup

Pass any `Print` object, before `dongle.begin()` so its first messages are not lost:

```cpp
#include "TinyWifiDongle.h"

TinyWifiDongle dongle;

void setup() {
  Serial.begin(115200);
  TWD_LOGGER.begin(Serial);  // any Print: Serial, Serial0, Serial1, a USBCDC, ...
  dongle.begin("my-ssid", "my-password");
}
```

The output has to be started by the sketch (`Serial.begin()`, ...); the logger only writes to it.

## Log levels

| Level | Used by the library for |
|---|---|
| `Debug` | (nothing yet) |
| `Info` | start, bridge MAC, WiFi associated, the status line (`statsIntervalMs`) |
| `Warning` | WiFi disconnected (ESP32 with the reason code); every 10 s while the Pico is not associated, with the chip's reason (network not found, wrong password, ...); the stall watchdog re-enumerating USB and the USB controller registers while the send queue is stuck (ESP32); the USB task could not be pinned (ESP32); the status line could not be started |
| `Error` | a part could not be started, so `begin()` returns false: no SSID, out of memory (send queue, frame buffers), timers or workers that could not be created, no free USB endpoints, the USB interface could not be registered or `USB.begin()` failed (ESP32), `WiFi.begin()` rejected the SSID or password (ESP32); also, after a connect, the WiFi receive path could not be taken over (ESP32) |

The level type is nested in the class, `twd::LoggerClass::LogLevel`, so it cannot clash with another library. Lines below the selected level are discarded:

```cpp
using LogLevel = twd::LoggerClass::LogLevel;

TWD_LOGGER.begin(Serial, LogLevel::Warning);  // only warnings and errors; the default is Info
TWD_LOGGER.setLevel(LogLevel::Debug);         // change it later
```

Frames that are dropped one by one (a full queue, no free buffer, the host not listening) are not logged, because that happens under normal load and logging it would slow the data path; they are counted in the status line instead.

`TinyWifiDongleSetup` logs to `TWD_LOGGER` as well, with lines that start with `setup:`. Info: the scanned networks, the address of the setup network, the number of connected devices, a press of the forget button. Debug: every request to the setup page, which shows what a phone does. Warning: the join timeout that forgets the network and restarts the board. Error: a failed scan, the setup network or DNS server that could not be started, a form without a network name, flash that could not be written.

## API

| Method | Meaning |
|---|---|
| `begin(Print& out, LogLevel level = Info)` | write lines of `level` and above to `out` |
| `end()` | stop logging |
| `flush()` | wait until the output has sent what was written (e.g. before a restart) |
| `setLevel(level)`, `level()` | set / read the lowest level that is written |
| `enabled(level = Info)` | true if lines of `level` are written somewhere |
| `debug(fmt, ...)`, `info(...)`, `warning(...)`, `error(...)` | printf-style line of that level |
| `log(level, fmt, ...)`, `vlog(level, fmt, va_list)` | the same with the level as an argument |

Sketches can use it for their own messages too, e.g. `TWD_LOGGER.info("free heap %u", ESP.getFreeHeap())`.

## Output format

```
[dongle] I starting: ssid='my-ssid'
[dongle] I bridge: host adapter uses MAC 24:58:7c:..:..:..
[dongle] W WiFi disconnected, reason=8
```

Each line starts with `[dongle]` and the first letter of its level (`D`, `I`, `W`, `E`), and ends with `\r\n`. A line is formatted into a 256-byte buffer (longer text is cut off) and passed to the output with a single `write()` call, so lines from different tasks do not interleave. There is no queue: the line is written from whatever task or context logs it, and whether that blocks while nobody listens depends on the output.

The core's own `log_x()` output (ESP32) is not redirected.

## Choosing the output

- __Hardware UART__ (`Serial0` on the ESP32, `Serial1` on GP0/GP1 on the Pico): needs no USB at all and never depends on a terminal being open. The right choice on boards with a USB-UART bridge (the ESP32-P4 dev boards) and the most reliable one for the status line.
- __USB serial port on the ESP32-S2/S3__ (`USBCDC`, started before `dongle.begin()`): lets the board be flashed without BOOT+RESET. Call `setTxTimeoutMs(0)` so it does not block while no terminal is open. Writing to it from tasks on the other CPU core than the USB interrupt can race with the interrupt in the core's TinyUSB driver and stall the network send path (see [architecture.md](architecture.md)), so prefer a UART for the status line.
- __USB serial port on the Raspberry Pi Pico__ (`Serial`): only transmits while a terminal program has the port open, so messages printed at the start are lost, and the periodic status line, which is printed from the WiFi chip's context, did not show up in the tests (not analysed). For reliable output use `Serial1` with a USB-serial adapter.

The fields of the status line are explained in [behaviour.md](behaviour.md).
