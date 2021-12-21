# ble_proxy_esphome

Bluetooth Low-Energy Xiaomi-specific ESPHome proxy for ESP32 devices. Proxies multiple BTLE devices to MQTT.

Tested with LYWSD03MMC thermometers - they're super-cheap.

Last update 2021-12-21. 
Written by John Mueller (johnmu.com)

## Goals

BTLE thermometers are cheap, and last a long time on battery. However, they use BTLE to communicate, which doesn't go far (10-15m? YMMV). ESP32 devices are also cheap, they support both BTLE as well as Wifi. This project proxies BTLE to MQTT via Wifi, supporting multiple devices that don't need to be configured individually. Using MQTT also enables basic fault tolerance. 

Add BTLE devices where you need them, add proxies nearby as needed. 

## Setup (super-rough)

1. Set up ESPhome 
2. Clone this repo
3. Flash the ATC firmware on your Xiaomi devices (removes bind keys)
4. Set up a MQTT server (eg, Home Assistant)
5. Compile & run this code on an ESP32
6. Repeat to place proxy devices in strategic locations

Note that when using the proxy, don't also read the BTLE values directly with Home Assistant (you'll just get duplicated sensors). Don't use the BTLE module there, only use MQTT.

## BTLE Device firmware

I used [pvvx's ATC firmware](https://github.com/pvvx/ATC_MiThermometer). This seems to work well on the Xiaomi Mi / LYWSD03MMC devices I have. 

The simplest way is to navigate to the [flasher page](https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html) on a smartphone, connect to the BTLE device, flash the firmware, and then configure the firmware to use "Mi" connections (this is compatible with all Xiaomi-like software).

Device lifetime seems to be 12-18 months on a CR2032 battery. Use the default settings with 2.5 seconds interval since ESP32 BTLE isn't that great.

## Proxy setup

I recommend using the default config file to start. Use unique device names for each ESP32 that you use, since these need to use the device name to communicate and for over-the-air updates. 

Before compiling, make a copy of "secrets-example.yaml" and call it "secrets.yaml". In this file, update the wifi SSID, password, and the MQTT hostname, username, password. 

Note: the MQTT communications are over HTTP and not encrypted. We're just sending temperature/humidity, so probably no big deal.

```
esphome -s name yourdevicename run ble_proxy_default.yaml
```

This will compile ESPhome based on the YAML-config file specified, using the device name 'yourdevicename'. I like to number my devices, but call them whatever you want.

The initial setup must be done with the device connected with a USB cable, afterwards you can update with OTA. 

I use a variety of ESP32 dev-boards, they're cheap, use USB, and are pretty small. I hang them from a USB charger with a short cable in out-of-the-way places, or from USB ports of routers, Raspberry-Pi's, etc. 

## MQTT

The proxies publish MQTT data in three places. 

A good way to understand the data is to use a MQTT explorer like [MQTT explorer](http://mqtt-explorer.com/). Connect to your MQTT server, and you'll see what's happening.

All data is published using the name of the ESP32 proxy as well as the MAC address of the BTLE device. You can rename MAC addresses as needed. 

### Device measurements

Measurements are published as:

/ble_proxy/[MAC Addr]/[measurement]/state

For example:

```
/ble_proxy/A4:C1:38:AA:BB:CC/battery_level/state
/ble_proxy/A4:C1:38:AA:BB:CC/temperature/state
```

Read these values to get the current measurement for each device. 

### Proxy status

Proxy status information is published under:

/[Proxyname]/seen/[MAC Addr]/
... in there, it includes:
name = BTLE device name
rssi = BTLE RSSI measurement
type = PUBLIC
viewcount = Number times device sent data in last hour

Example:

```
/btproxy05/seen/A4:C1:38:AA:BB:CC/name
/btproxy05/seen/A4:C1:38:AA:BB:CC/rssi
/btproxy05/seen/A4:C1:38:AA:BB:CC/viewcount
```

### Autodiscover for Home Assistant

For Home Assistant, the autodiscovery config information is published:

/homeassistant/sensor/ble_proxy/[MAC Addr]__[measurement]

This looks a bit messy in MQTT but makes sure that all measurements are automatically found in Home Assistant. 

Examples:

```
/homeassistant/sensor/ble_proxy/a4_c1_38_aa_bb_cc__temperature/config
/homeassistant/sensor/ble_proxy/a4_c1_38_aa_bb_cc__humidity/config
/homeassistant/sensor/ble_proxy/a4_c1_38_aa_bb_cc__temperature/config
```

## Advanced configuration

Requirements:
* mqtt (hostname, username, password)
* esp32_ble_tracker (no settings)

Common setup in a YAML file:

```
sensor:
  - platform: ble_proxy
    hostname: ${name}
    mqtt_client_id: mqtt_client
```

Required settings:

* hostname - name of this ESP32 device, used for MQTT connections
* mqtt_client_id - ID of the MQTT client that you set up

Optional settings:

* auto_reboot_interval
  Time to automatically reboot the ESP32. Since we track seen BTLE devices, this encourages us not to run out of memory over time. 

  Example:

  ```auto_reboot_interval: 6h```

* mac_addresses_renamed
  MAC addresses are great until you have to swap out a device. With this setting, you can rename devices to us a different name. You can also use this to give BTLE devices an understandable name.
  Uses YAML lists with strings.

  Examples:

  ```mac_addresses_renamed: "A4:C1:38:00:11:22=A4:C1:38:AA:BB:CC"```

  ```
  mac_addresses_renamed:
   - "A4:C1:38:00:11:22=A4:C1:38:AA:BB:CC"
   - "A4:C1:38:00:22:44=GUESTROOM"
  ```

* mac_addresses_allowed
  If you have a lot of BTLE devices and *only* want to proxy a portion of them, specify them like this. Allowed MACs are processed before renames.

  Examples:

  ```mac_addresses_allowed: ["A4:C1:38:00:11:22", "A4:C1:38:00:22:44"]```

  ```
  mac_addresses_allowed:
   - "A4:C1:38:00:11:22"
   - "A4:C1:38:00:22:44"
  ```

* mac_addresses_blocked
  If you have a lot of BTLE devices and *don't* want to proxy a portion of them, specify them like this. All other devices are proxied. Blocked MACs are processed before renames.

  Examples:

  ```mac_addresses_blocked: ["A4:C1:38:00:11:22", "A4:C1:38:00:22:44"]```

  ```
  mac_addresses_blocked:
   - "A4:C1:38:00:11:22"
   - "A4:C1:38:00:22:44"
  ```

## Supported BTLE devices

Theoretically this supports various Xiaomi BTLE devices. I only have the thermometers. Aliexpress or a local electronics shop is your friend.

You must remove the bind-keys for this to work (with the previously-mentioned firmware). The device will continue to work as previously without bind-keys, you can use any Xiaomi-supported app to also read the devices.

This code proxies measurements for:

* temperature
* humidity
* battery-level
* conductivity
* illuminance
* moisture
* tablet
* "is active" (a switch?)
* "has motion" (another switch?)
* "is light" (yet another switch?)

## Updates

* 2021-12-21 - initial commit (has been running for >1 year now)
