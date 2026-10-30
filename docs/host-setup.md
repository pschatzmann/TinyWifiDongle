# Host setup tips

- If the PC is also connected to the same WiFi network, give the dongle connection a **higher route metric than WiFi** (e.g. `nmcli con mod uuid <profile> ipv4.route-metric 900 ipv4.dns-priority 200`). Otherwise the PC prefers the dongle for all traffic. NetworkManager creates a new profile for every new adapter name (the name contains the board's MAC), so repeat this for a new board, and check which profile is really active (`nmcli dev show <iface>`).
- With both interfaces on the same subnet, NetworkManager sometimes does not install the dongle's default route. `nmcli con up uuid <profile> ifname <iface>` brings it back; with WiFi off the dongle works as the only connection.
- Wake-on-LAN settings have no effect: the adapter's driver has no support for it and the device does not declare remote wakeup.
