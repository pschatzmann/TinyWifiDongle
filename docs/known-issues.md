# Known issue: the USB stall

Reduced by `pinUsbTask`, not eliminated.

The Arduino core's TinyUSB (0.21.0) has a race between its USB task and the USB interrupt when they run on different CPU cores; a bulk IN transfer then loses its "TX FIFO empty" interrupt and never completes. Without the workaround, sustained transfers to the host stall after a few hundred frames (a few KB/s with regular reconnects). Running the task on the interrupt's core makes the stall rare: in the tests it came once per roughly 100-200 MB (at 530-615 KB/s). With the watchdog on (default), such a stall costs a second or two and a USB reconnect: with the library defaults, 59 of 60 consecutive 3 MB downloads (177 MB) completed and the one that was hit was followed by 16 successful ones. The cause of the remaining stalls is not known yet. The full analysis, the upstream fixes and the experiments are in [architecture.md](architecture.md).
