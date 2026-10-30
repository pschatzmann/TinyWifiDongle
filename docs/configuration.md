# Configuration

Pass a `TinyWifiDongleConfig` to `begin()` (all fields except `ssid` are optional):

| Field | Default | Meaning |
|---|---|---|
| `ssid`, `password` | - | WiFi network to join (`password = nullptr` for an open network) |
| `productName` | `"TinyWifiDongle"` | USB product string shown on the host |
| `manageUsb` | `true` | the library calls `USB.begin()`; set `false` if the sketch adds other USB interfaces and starts USB itself |
| `pinUsbTask` | `true` | run the TinyUSB task on the CPU core of the USB interrupt (greatly reduces a driver race, see [known-issues.md](known-issues.md)); needs `manageUsb` |
| `stallTimeoutMs` | `1000` | safety net: re-enumerate USB if sending to the host stalls this long (the host sees a replug); `0` = never reset |
| `logTarget` | `Default` | where messages go: `Uart0` (hardware serial `Serial0`, 115200 baud), `UsbCdc` (a USB serial port next to the adapter) or `None`. `Default` is `UsbCdc` on the ESP32-S2/S3 and `Uart0` on the ESP32-P4 (see [behaviour.md](behaviour.md)) |
| `statsIntervalMs` | `0` | interval of the status line in ms, `0` = never. Only useful while debugging |
| `usbMaxFrame` | `1514` | largest Ethernet frame announced to the host (limits the host MTU) |

`TinyWifiDongle` also offers `isUsbReady()` (the host has configured the adapter) and `isWifiConnected()` (associated with the access point).
