# Host setup tips

Applies to every platform; the examples use Linux with NetworkManager.

- If the PC is also connected to the same WiFi network, give the dongle connection a **higher route metric than WiFi** (e.g. `nmcli con mod uuid <profile> ipv4.route-metric 900 ipv4.dns-priority 200`). Otherwise the PC prefers the dongle for all traffic. NetworkManager creates a new profile for every new adapter name (the name contains the board's MAC), so repeat this for a new board, and check which profile is really active (`nmcli dev show <iface>`).
- With both interfaces on the same subnet, NetworkManager sometimes installs the routes for only one of them. Either the dongle has no default route, or the PC's WiFi has lost its own and all traffic goes through the dongle. Reactivate the connection that lacks it: `nmcli con up uuid <profile> ifname <iface>`. Check with `ip route`. With WiFi off the dongle works as the only connection.
- Wake-on-LAN settings have no effect: the adapter's driver has no support for it and the device does not declare remote wakeup.
- If large transfers hang on your network (a path with a smaller MTU than 1500), set the host MTU of the adapter to 1400: `nmcli con mod uuid <profile> 802-3-ethernet.mtu 1400`, then `nmcli dev reapply <iface>`. Both the ESP32 (`usbMaxFrame` 1514) and the Pico announce a maximum of 1514, so 1400 is allowed.
- `ifconfig` (net-tools) shows zero packet counters for the adapter, because its name (`enx` plus the 12-digit MAC) has the maximum length of 15 characters. The traffic is real; read the counters with `ip -s link show <iface>` or `/proc/net/dev`.
