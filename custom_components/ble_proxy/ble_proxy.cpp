/*
Copyright 2021-2023 John Mueller
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

#include "ble_proxy.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <esp_bt_main.h>
#include <esp_bt.h>
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

// #ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace ble_proxy {

static const char *TAG = "ble_proxy";
static const char *MQTT_BASE = "ble_proxy";

/* Handle BLE device, check if we need to track & forward it
*/
bool BLE_PROXY::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  // skip random BLE devices
  esp_ble_addr_type_t d_type = device.get_address_type();
  if ((d_type==BLE_ADDR_TYPE_RANDOM) || (d_type==BLE_ADDR_TYPE_RPA_RANDOM)) return false;

  bool success = false;
  for (auto &service_data : device.get_service_datas()) {
    auto res = xiaomi_ble::parse_xiaomi_header(service_data);
    if (!res.has_value()) continue;
    if (res->is_duplicate) continue;
    if (res->has_encryption) continue;
    if (!(this->can_track(device))) continue;

    this->seen_device(device);

    if (!(xiaomi_ble::parse_xiaomi_message(service_data.data, *res))) {
      continue;
    }

    if (res->humidity.has_value()) {
      // see https://github.com/custom-components/sensor.mitemp_bt/issues/7#issuecomment-595948254
      *res->humidity = trunc(*res->humidity);
    }
    if (!(xiaomi_ble::report_xiaomi_results(res, device.address_str()))) {
      continue;
    }

    // all types from xiaomi_ble.cpp
    if (res->temperature.has_value()) {
      this->notify_data(device, "temperature", *res->temperature);
    }
    if (res->humidity.has_value()) {
      this->notify_data(device, "humidity", *res->humidity);
    }
    if (res->battery_level.has_value()) {
      this->notify_data(device, "battery_level", *res->battery_level);
    }
    if (res->conductivity.has_value()) {
      this->notify_data(device, "conductivity", *res->conductivity);
    }
    if (res->illuminance.has_value()) {
      this->notify_data(device, "illuminance", *res->illuminance);
    }
    if (res->moisture.has_value()) {
      this->notify_data(device, "moisture", *res->moisture);
    }
    if (res->tablet.has_value()) {
      this->notify_data(device, "tablet", *res->tablet);
    }
    if (res->is_active.has_value()) { // (*res->is_active) ? "on" : "off");
      this->notify_data(device, "is_active", (*res->is_active)?1:0);
    }
    if (res->has_motion.has_value()) { // (*res->has_motion) ? "yes" : "no");
      this->notify_data(device, "has_motion", (*res->has_motion)?1:0);
    }
    if (res->is_light.has_value()) { //(*res->is_light) ? "on" : "off");
      this->notify_data(device, "is_light", (*res->is_light)?1:0);
    }

    success = true;
  }

  check_auto_reboot();
  notify_seen_devices();

  return success; // unless we rebooted
}

/* Update MQTT on seen devices in last hour
*/
void BLE_PROXY::notify_seen_devices() {
  const int notify_interval = 60*60*1000; // notify every hour
  if ((millis() > this->seen_devices_notify_millis_ ) 
    || (millis()+notify_interval < this->seen_devices_notify_millis_)) {
    for (const auto& kv: this->seen_devices_) {
      ESP_LOGD(TAG, "Seen '%s' %i times last hour", kv.first.c_str(), kv.second);
      std::string topic(this->hostname_ + "/seen/" + kv.first);
      std::string value(to_string(kv.second));
      this->mqtt_parent_->publish(topic + "/viewcount", value, 0, true);
      this->seen_devices_[kv.first] = 0;
    }
    this->seen_devices_notify_millis_ = millis() + notify_interval;
  }
}

/* Check if it's time to reboot this device
*/
void BLE_PROXY::check_auto_reboot() {
  if (this->reboot_millis_>0) {
    if (millis() > this->reboot_millis_) { 
      // no overflow, since we're counting from boot
      ESP_LOGI(TAG, "Rebooting now.");
      delay(500); // Let MQTT settle a bit
      App.safe_reboot();    
    }
  }
}

/* Get name of this device, taking into account mappings
*/
std::string BLE_PROXY::get_device_name(const esp32_ble_tracker::ESPBTDevice &device) {
  std::string mymac(device.address_str());  // MAC address
  for (auto elem : this->macs_rename_) {
    if (elem.find(mymac + "=") == 0) { // rename entry starts with MAC address
      mymac = elem.substr(mymac.length()+1); // use part after '='
    }
  }
  return(mymac);
}

/* Check if we can track this device
*/
bool BLE_PROXY::can_track(const esp32_ble_tracker::ESPBTDevice &device) {
  std::string mymac(device.address_str());  // MAC address
  if (!(this->macs_allowed_.empty())) { // allow-list exists, must follow it
    if (this->macs_allowed_.find(mymac) == this->macs_allowed_.end()) {
      ESP_LOGD(TAG, "Device not trackable: '%s' not in allow list", mymac.c_str());
      return(false);
    }
  }
  if (this->macs_disallowed_.find(mymac) != this->macs_disallowed_.end()) {
    ESP_LOGD(TAG, "Device not trackable: '%s' is in disallow list", mymac.c_str());
    return(false);
  }
  return(true);
}

/* Remember & notify that we've seen a specific BLE device
*/
void BLE_PROXY::seen_device(const esp32_ble_tracker::ESPBTDevice &device) {
  std::string seen_device(this->get_device_name(device));

  if (this->seen_devices_.find(seen_device)!=this->seen_devices_.end()) {
    this->seen_devices_[seen_device]++;
    return;
  }
  if (!this->mqtt_parent_->is_connected()) return;

  this->seen_devices_[seen_device] = 1;
  std::string topic(this->hostname_ + "/seen/" + seen_device);
  this->mqtt_parent_->publish(topic + "/name", device.get_name(), 0, true);
  this->mqtt_parent_->publish(topic + "/rssi", to_string(device.get_rssi()), 0, true);

  // address type
  std::string value;
  switch (device.get_address_type()) {
    case BLE_ADDR_TYPE_PUBLIC:
      value = "PUBLIC";
      break;
    case BLE_ADDR_TYPE_RANDOM:
      value = "RANDOM";
      break;
    case BLE_ADDR_TYPE_RPA_PUBLIC:
      value = "RPA_PUBLIC";
      break;
    case BLE_ADDR_TYPE_RPA_RANDOM:
      value = "RPA_RANDOM";
      break;
  }
  this->mqtt_parent_->publish(topic + "/type", value, 0, true);

  ESP_LOGD(TAG, "MQTT published: %s = %s", topic.c_str(), value.c_str());
}

/* Notify of new dataset, check if publishable
*/
void BLE_PROXY::notify_data(const esp32_ble_tracker::ESPBTDevice &device, 
    std::string label, double value) {
  ESP_LOGD(TAG, "notify_data: Device '%s', Attribute '%s' = %.1f", device.address_str().c_str(), 
    label.c_str(), value);

  // check if seen
  std::string dev_addr(this->get_device_name(device));
  std::string dev_sensor(dev_addr + "/" + label);
  bool new_device = !(sensors_last_notified_.count(dev_sensor));
  if (new_device) { // new sensor, send immediately, do auto-discovery
    ESP_LOGD(TAG, "notify_data: Device '%s' is new", dev_sensor.c_str());

    bool ok = this->send_data_mqtt(device, label, value, true);
    if (ok) {    
      sensors_value_sum_[dev_sensor] = 0.0;
      sensors_value_count_[dev_sensor] = 0;
      sensors_last_notified_[dev_sensor] = millis();
    }
  } else { // known sensor, potentially average values
    sensors_value_sum_[dev_sensor] += value;
    sensors_value_count_[dev_sensor]++;
    if (millis() - sensors_last_notified_[dev_sensor] > notify_interval_millis_) {
      ESP_LOGD(TAG, "notify_data: Device '%s' send data, %i measurements, %.1f average",
        dev_sensor.c_str(), sensors_value_count_[dev_sensor], 
        sensors_value_sum_[dev_sensor] / sensors_value_count_[dev_sensor]);

      // time to send data, reset counters
      this->send_data_mqtt(device, label, 
        sensors_value_sum_[dev_sensor] / sensors_value_count_[dev_sensor], false);
      sensors_value_sum_[dev_sensor] = 0.0;
      sensors_value_count_[dev_sensor] = 0;
      sensors_last_notified_[dev_sensor] = millis();
    }
  }
}

/* Send the current datapoint to MQTT
 * returns true if successful
*/
bool BLE_PROXY::send_data_mqtt(const esp32_ble_tracker::ESPBTDevice &device, 
    std::string label, double value, bool new_device) {
  ESP_LOGD(TAG, "Sending to MQTT: Device '%s', Attribute '%s' = %.1f", device.address_str().c_str(), 
    label.c_str(), value);
  if (!this->mqtt_parent_->is_connected()) {
    ESP_LOGD(TAG, "MQTT not connected.");
    return false;
  }
  std::string primary(MQTT_BASE);
  std::string dev_addr(this->get_device_name(device));
  std::string topic(primary + "/" + dev_addr + "/" + label + "/" + "state");
  std::string vala(value_accuracy_to_string(value, 1));

  if (new_device) {
    this->send_autodiscovery(dev_addr, topic, label);
  }
  this->mqtt_parent_->publish(topic, vala, 0, true);
  ESP_LOGD(TAG, "MQTT published: %s = %s", topic.c_str(), vala.c_str());
  return true;
}

/* Send datapoint for autodiscovery for Homeassistant, using default endpoint
*/
void BLE_PROXY::send_autodiscovery(std::string device, std::string topic, std::string label) {
  ESP_LOGD(TAG, "MQTT Autodiscovery");

  std::string homeroot("homeassistant/sensor");
  std::string nodeid(MQTT_BASE);
  std::string objectid("");
  std::string rawobjectid(device + "__" + label);
  for (int i=0; i<rawobjectid.length(); i++) {
    char part = tolower(rawobjectid[i]);
    if (isalnum(part)) objectid += part; else objectid += "_";
  }
  std::string configtopic(homeroot + "/" + nodeid + "/" + objectid + "/config");
  std::string units("");
  std::string icon("");
  if (label=="temperature") {
    units = "°C"; icon = "mdi:thermometer";
  } else if (label=="humidity") {
    units = "%"; icon = "mdi:water-percent";
  } else if (label=="battery_level") {
    units = "%"; icon = "mdi:battery";
  } else if (label=="conductivity") {
    units = "µS/cm"; icon = "";
  } else if (label=="illuminance") {
    units = "lx"; icon = "";
  } else if (label=="moisture") {
    units = "%"; icon = "mdi:water-percent";
  } else if (label=="tablet") {
    units = "%"; icon = "";
  }

  std::string data; // artisinal json
  data = "{ ";
  data += "\"unit_of_measurement\": \"" + units + "\", ";
  if (icon != "") {
    data += "\"icon\": \"" + icon + "\", ";
  }
  data += "\"name\": \"" + device + " " + label + "\", ";
  data += "\"state_topic\": \"" + topic + "\", ";
  data += "\"unique_id\": \"" + objectid + "\", ";
  data += "\"device\": { ";
  data += "\"identifiers\": \"" + device + "\", ";
  data += "\"name\": \"" + device + "\", ";
  data += "\"model\": \"ble_proxy\", ";
  data += "\"manufacturer\": \"johnmu\" ";
  data += " } ";
  data += " } ";
  this->mqtt_parent_->publish(configtopic, data, 0, true);
  ESP_LOGD(TAG, "MQTT published: %s = %s", configtopic.c_str(), data.c_str());
}

void BLE_PROXY::update_ble_enabled(bool enabled_yes) {
  esp_err_t err;
  ESP_LOGD(TAG, "update_ble_enable to %i", (enabled_yes?1:0));
  if (enabled_yes) { // Enable BLE
    ESP_LOGD(TAG, "running esp32_ble_tracker::global_esp32_ble_tracker->setup()");
    esp32_ble_tracker::global_esp32_ble_tracker->setup();
    ESP_LOGD(TAG, "esp32_ble_tracker::global_esp32_ble_tracker->setup() complete");
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
      ESP_LOGD(TAG, "BLE already enabled, can't enable!");
    } else {
      ESP_LOGD(TAG, "BLE currently not enabled, trying to enable");
      // assuming we use standard esp32_ble_tracker init ...
      err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(err));
      } else {
        ESP_LOGD(TAG, "BLE now enabled");
      }
    }
  } else { // Disable BLE
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
      ESP_LOGD(TAG, "BLE currently enabled, will disable");
      err = esp_bt_controller_disable();
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE esp_bt_controller_disable failed: %s", esp_err_to_name(err));
      } else {
        ESP_LOGD(TAG, "BLE disabled.");
        ESP_LOGD(TAG, "BLE will now deinit() ...");
        err = esp_bt_controller_deinit(); // kill it all
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "BLE esp_bt_controller_deinit failed: %s", esp_err_to_name(err));
        } else {
          ESP_LOGD(TAG, "BLE esp_bt_controller_deinit successful.");
        }
      }
    } else {
      ESP_LOGD(TAG, "BLE NOT enabled, can't disable!");
    }
  }
}

}  // namespace ble_proxy
}  // namespace esphome

// #endif
