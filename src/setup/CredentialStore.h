#pragma once
#include <Arduino.h>
#include <EEPROM.h>

namespace twd {

/**
 * @brief Keeps the name and password of one WiFi network in flash.
 *
 * Uses the EEPROM emulation that both the ESP32 and the Raspberry Pi Pico
 * cores provide, so no filesystem partition is needed. The data is marked, so
 * flash that was never written by this class reads as "nothing saved".
 */
class CredentialStore {
 public:
  /// Reads the flash. Returns true if a network is saved.
  bool load() {
    begin();
    EEPROM.get(0, data_);
    return valid();
  }

  /// True if a network is saved (after load() or save()).
  bool valid() const { return data_.magic == kMagic && data_.ssid[0] != 0; }

  /// Saves the network; an empty or null password means an open network.
  void save(const char* ssid, const char* password) {
    memset(&data_, 0, sizeof(data_));
    data_.magic = kMagic;
    snprintf(data_.ssid, sizeof(data_.ssid), "%s", ssid ? ssid : "");
    snprintf(data_.password, sizeof(data_.password), "%s", password ? password : "");
    write();
  }

  /// Removes the saved network.
  void forget() {
    memset(&data_, 0, sizeof(data_));
    write();
  }

  /// Name of the saved network.
  const char* ssid() const { return data_.ssid; }

  /// Password of the saved network, nullptr if it is an open network.
  const char* password() const { return data_.password[0] ? data_.password : nullptr; }

 private:
  static constexpr uint32_t kMagic = 0x54574431;  // marks the structure as ours
  struct Data {
    uint32_t magic;
    char ssid[33];
    char password[65];
  };

  Data data_ = {};
  bool begun_ = false;

  void begin() {
    if (begun_) return;
    EEPROM.begin(sizeof(Data));
    begun_ = true;
  }

  void write() {
    begin();
    EEPROM.put(0, data_);
    EEPROM.commit();
  }
};

}  // namespace twd
