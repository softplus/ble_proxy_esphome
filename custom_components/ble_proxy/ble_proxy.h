/*
Copyright 2021 John Mueller
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <map>
#include <set>

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/xiaomi_ble/xiaomi_ble.h"
#include "esphome/components/mqtt/mqtt_client.h"

// #ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace ble_proxy {

class BLE_PROXY : public Component, public esp32_ble_tracker::ESPBTDeviceListener {
 public:
  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_hostname(const std::string &hostname) { hostname_ = hostname; };
  void add_macs_allowed(const std::string &item) { macs_allowed_.insert(item); };
  void add_macs_disallowed(const std::string &item) { macs_disallowed_.insert(item); };
  void add_macs_renamed(const std::string &item) { macs_rename_.insert(item); };
  void set_mqtt_parent(mqtt::MQTTClientComponent *parent) { mqtt_parent_ = parent; }
  void set_reboot_interval(uint32_t update_interval) { reboot_millis_ = millis() + update_interval; }

 protected:
  void send_data(const esp32_ble_tracker::ESPBTDevice &device, 
    std::string label, double value);
  std::string get_device_name(const esp32_ble_tracker::ESPBTDevice &device);
  bool can_track(const esp32_ble_tracker::ESPBTDevice &device);
  void send_autodiscovery(std::string device, std::string topic, std::string label);
  void seen_device(const esp32_ble_tracker::ESPBTDevice &device);
  void check_auto_reboot();
  void notify_seen_devices();
  mqtt::MQTTClientComponent  *mqtt_parent_;
  std::string hostname_;
  std::map<std::string, int> seen_devices_;
  std::set<std::string> macs_allowed_;
  std::set<std::string> macs_disallowed_;
  std::set<std::string> macs_rename_;
  long unsigned int reboot_millis_ = 0;
  long unsigned int seen_devices_notify_millis_ = 0;
};

}  // namespace ble_proxy
}  // namespace esphome

// #endif
