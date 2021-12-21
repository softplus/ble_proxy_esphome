import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, esp32_ble_tracker, mqtt
from esphome.const import CONF_BATTERY_LEVEL, CONF_HUMIDITY, CONF_MAC_ADDRESS, \
    CONF_TEMPERATURE, UNIT_CELSIUS, ICON_THERMOMETER, UNIT_PERCENT, ICON_WATER_PERCENT, \
    ICON_BATTERY, CONF_ID

CODEOWNERS = ['@johnmu', '@ahpohl']

DEPENDENCIES = ['esp32_ble_tracker', 'mqtt']
AUTO_LOAD = ['xiaomi_ble']

CONF_MQTT_PARENT_ID = 'mqtt_client_id'
CONF_HOSTNAME = 'hostname'
CONF_MACS_ALLOWED = 'mac_addresses_allowed'
CONF_MACS_DISALLOWED = 'mac_addresses_blocked'
CONF_MACS_RENAME = 'mac_addresses_renamed'
CONF_AUTO_REBOOT = 'auto_reboot_interval'

ble_proxy_ns = cg.esphome_ns.namespace('ble_proxy')
BLE_PROXY = ble_proxy_ns.class_('BLE_PROXY',
                        esp32_ble_tracker.ESPBTDeviceListener,
                        cg.Component)


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(BLE_PROXY),
    cv.Required(CONF_HOSTNAME): cv.hostname,
    cv.Optional(CONF_MACS_ALLOWED): cv.ensure_list(cv.string),
    cv.Optional(CONF_MACS_DISALLOWED): cv.ensure_list(cv.string),
    cv.Optional(CONF_MACS_RENAME): cv.ensure_list(cv.string),
    cv.Optional(CONF_AUTO_REBOOT): cv.positive_time_period_milliseconds,
    cv.GenerateID(CONF_MQTT_PARENT_ID): cv.use_id(mqtt.MQTTClientComponent),
}).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield esp32_ble_tracker.register_ble_device(var, config)

    parent = yield cg.get_variable(config[CONF_MQTT_PARENT_ID])
    cg.add(var.set_mqtt_parent(parent))
    cg.add(var.set_hostname(config[CONF_HOSTNAME]))
    
    if CONF_MACS_ALLOWED in config:
        for item in config[CONF_MACS_ALLOWED]:
            cg.add(var.add_macs_allowed(item))
    if CONF_MACS_DISALLOWED in config:
        for item in config[CONF_MACS_DISALLOWED]:
            cg.add(var.add_macs_disallowed(item))
    if CONF_MACS_RENAME in config:
        for item in config[CONF_MACS_RENAME]:
            cg.add(var.add_macs_renamed(item))
    if CONF_AUTO_REBOOT in config:
        cg.add(var.set_reboot_interval(config[CONF_AUTO_REBOOT]))
