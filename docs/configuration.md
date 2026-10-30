# Configuration

Pass a `TinyWifiDongleConfig` to `begin()` (all fields except `ssid` are optional). The column "Platform" says where a setting has an effect:

| Field | Default | Platform | Meaning |
|---|---|---|---|
| `ssid`, `password` | - | all | WiFi network to join (`password = nullptr` for an open network) |
| `productName` | `"TinyWifiDongle"` | all | USB product string shown on the host |
| `logTarget` | `Default` | all | where messages go: `Uart0`, `UsbCdc` or `None`, see below |
| `statsIntervalMs` | `0` | all | interval of the status line in ms, `0` = never. Only useful while debugging |
| `manageUsb` | `true` | ESP32 | the library calls `USB.begin()`; set `false` if the sketch adds other USB interfaces and starts USB itself |
| `pinUsbTask` | `true` | ESP32 | run the TinyUSB task on the CPU core of the USB interrupt (greatly reduces a driver race, see [known-issues.md](known-issues.md)); needs `manageUsb` |
| `stallTimeoutMs` | `1000` | ESP32 | safety net: re-enumerate USB if sending to the host stalls this long (the host sees a replug); `0` = never reset |
| `usbMaxFrame` | `1514` | ESP32 | largest Ethernet frame announced to the host (limits the host MTU) |
| `framesToHost`, `framesFromHost` | `0` | Pico | number of frame buffers (about 1.5 KB each) for frames on their way to the host and from the host. `0` = the default of the chip: 48 and 16 on the RP2350 (Pico 2 W), 24 and 8 on the RP2040 (Pico W). More buffers absorb bursts of traffic, so fewer frames are dropped, at the price of RAM. The directions have separate pools, so a burst towards the host cannot take the buffers that the host's acknowledgements need |

The settings marked ESP32 are accepted but ignored on the Raspberry Pi Pico, and the settings marked Pico are ignored on the ESP32.

## Log target

| `logTarget` | ESP32-S2/S3 | ESP32-P4 | Raspberry Pi Pico W / 2 W |
|---|---|---|---|
| `Default` | = `UsbCdc` | = `Uart0` | = `UsbCdc` |
| `UsbCdc` | USB serial port next to the adapter; lets the board be flashed without BOOT+RESET | (not used) | `Serial`: USB serial port next to the adapter |
| `Uart0` | `Serial0`, 115200 baud (GPIO43/44 on most S3 boards; needs a USB-serial adapter) | `Serial0`, 115200 baud; the USB-UART bridge of the dev board | `Serial1`, 115200 baud (GP0 transmit, GP1 receive; needs a USB-serial adapter) |
| `None` | no output, no extra USB serial port | | |

See [behaviour.md](behaviour.md) for what is printed.

## Methods

`TinyWifiDongle` also offers `isUsbReady()` (the host has configured the adapter) and `isWifiConnected()` (associated with the access point).
