#pragma once
// Raspberry Pi Pico W / Pico 2 W port (arduino-pico core, USB Stack: Pico SDK).
#include <Arduino.h>

#if !defined(PICO_CYW43_SUPPORTED)
#error "TinyWifiDongle: this board has no WiFi chip. Select a Raspberry Pi Pico W or Pico 2 W."
#endif

#include "rp2040/Rp2040Dongle.h"

using TinyWifiDonglePort = twd::Rp2040Dongle;
