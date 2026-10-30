#pragma once
/**
 * RP2040 W port - PLANNED, not implemented yet.
 *
 * What it needs (see docs/architecture.md), everything else is shared:
 *  - UsbTransport: CDC-NCM on the arduino-pico core with the Adafruit TinyUSB
 *    stack (same tud_network_* callbacks as the ESP32 port, different
 *    descriptor registration).
 *  - FrameLink + Uplink: the CYW43 chip in station mode with raw Ethernet
 *    access (cyw43_send_ethernet / cyw43_cb_process_ethernet), taking the
 *    CYW43 MAC address for the USB adapter.
 *  - A composition class like Esp32Dongle, aliased to TinyWifiDonglePort.
 * L2Bridge, Logger and the facade are reused unchanged.
 */
#error "TinyWifiDongle: the RP2040 W port is planned but not implemented yet. See docs/architecture.md."
