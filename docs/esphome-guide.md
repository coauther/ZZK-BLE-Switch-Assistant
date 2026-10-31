# ESPHome / Home Assistant Integration Guide

**English** | [中文](esphome-guide.zh-CN.md)

The recommended way to integrate ZZK-SA2 into Home Assistant is by using an always-powered ESP32 development board running [ESPHome](https://esphome.io/) as a BLE gateway (BLE client).

This guide provides the complete configuration breakdown, covering the physical switch entity, battery sensor, connection state monitoring, external binary sensors (e.g. door reed switch), and the 180-second auto-reconnecting OTA maintenance mode.

---

## 1. Prerequisites: Finding the Device MAC Address

You can discover the Bluetooth MAC address of ZZK-SA2 using either of the following methods:

1. **Mobile App Scanning**: Use a BLE tool like **nRF Connect for Mobile** on your phone, scan for nearby devices, find `ZZK-SA2` (or `ZZK-SA2-XXXX`), and remember its MAC address (e.g., `C1:23:45:67:89:AB`).
2. **ESPHome Console Logs**: Add `esp32_ble_tracker:` to your ESP32 configuration and flash it. In the serial/wireless logs, locate the captured `[ZZK-SA2]` advertisement and its corresponding MAC address.

---

## 2. Feature Configuration

### A. BLE Client and Connection Events (`ble_client`)
Use `ble_client` to establish an active connection to ZZK-SA2, driving the connection status entity via `on_connect` and `on_disconnect` callbacks:

```yaml
esp32_ble_tracker:

ble_client:
  - mac_address: "C1:XX:XX:XX:XX:XX"    # Replace with your device's actual MAC address
    id: zzk_sa2
    auto_connect: true
    on_connect:
      then:
        - binary_sensor.template.publish:
            id: zzk_ble_connected
            state: ON
    on_disconnect:
      then:
        - binary_sensor.template.publish:
            id: zzk_ble_connected
            state: OFF
```

### B. Connection Status (`binary_sensor`)
Provides real-time visibility into the device's BLE connection state in Home Assistant (`device_class: connectivity`):

```yaml
binary_sensor:
  - platform: template
    name: "ZZK Connected"
    id: zzk_ble_connected
    device_class: connectivity
```

### C. Physical Switch Entity (`switch`)
Writes 1 byte to the custom switch control characteristic (`0x01` for ON / `0x00` for OFF):

```yaml
switch:
  - platform: template
    name: "ZZK Switch"
    optimistic: true
    turn_on_action:
      - ble_client.ble_write:
          id: zzk_sa2
          service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
          characteristic_uuid: "2d220001-516b-47bd-a33b-2c93889ac9b7"
          value: [0x01]
    turn_off_action:
      - ble_client.ble_write:
          id: zzk_sa2
          service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
          characteristic_uuid: "2d220001-516b-47bd-a33b-2c93889ac9b7"
          value: [0x00]
```

> [!NOTE]
> **`optimistic: true` is mandatory**: Because ZZK-SA2 is an open-loop, stateless mechanical actuator, the firmware does not report command execution results or physical wall switch positions. Setting this option prevents the Home Assistant frontend switch UI from bouncing back immediately after actuation.

### D. Battery Level Sensor (`sensor`)
Subscribes to notifications on the battery characteristic, reporting remaining percentage (`0~100`):

```yaml
sensor:
  - platform: ble_client
    ble_client_id: zzk_sa2
    name: "ZZK Battery"
    type: characteristic
    device_class: battery
    unit_of_measurement: "%"
    accuracy_decimals: 0
    service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
    characteristic_uuid: "2d220002-516b-47bd-a33b-2c93889ac9b7"
    notify: true
    update_interval: 300s
    force_update: true
    lambda: |-
      if (x.size() > 0) {
        return (float)x[0];
      }
      return {};
```

### E. External Binary Sensors (e.g., Door Reed Switch / Motion)
Subscribes to notifications on characteristic `2d220003-516b-47bd-a33b-2c93889ac9b7`, unpacks the 1-byte state bitmask inside `lambda`, and publishes directly to Home Assistant template binary sensor entities:

```yaml
binary_sensor:
  # Door sensor entity (Home Assistant assigns door icon and Open/Closed state)
  - platform: template
    name: "ZZK Door Sensor"
    id: zzk_door
    device_class: door

sensor:
  # Raw binary sensor bitmask decoder
  - platform: ble_client
    ble_client_id: zzk_sa2
    id: zzk_binary_raw
    type: characteristic
    internal: true                    # Hide raw numeric entity from Home Assistant dashboard
    service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
    characteristic_uuid: "2d220003-516b-47bd-a33b-2c93889ac9b7"
    notify: true
    update_interval: 60s
    force_update: true
    lambda: |-
      if (!x.empty()) {
        uint8_t raw = x[0];
        // Bit 0 mapped to door sensor (HA convention: true=Open, false=Closed)
        bool is_open = (raw & (1 << 0)) != 0;
        id(zzk_door).publish_state(is_open);
        return (float)raw;
      }
      return {};
```

> [!IMPORTANT]
> **ESPHome GATT Attribute Caching**:
> If your ESP32 node runs `bluetooth_proxy: active: true`, the underlying ESP-IDF stack caches peer GATT services in NVS flash by default. If the ESP32 connected to ZZK-SA2 prior to adding the new characteristic, it may read the stale cache and fail to discover `2d220003` (logging `Cannot poll, not connected`).
> **Solution**: Add `cache_services: false` under `bluetooth_proxy` to disable persistent caching and force fresh over-the-air service discovery upon connection.

---

## 3. Maintenance Mode to Prevent BLE Preemption

### Why is Maintenance Mode Required?
The nRF52840 SoC operates as a **single-peripheral BLE device**: once the ESP32 gateway establishes a connection, ZZK-SA2 **immediately stops advertising** to save power and follow the Bluetooth specification.  
If you want to update firmware via the PC script `ota_flash.py` or a mobile app, neither tool will be able to discover the device.

### The Solution: 180-Second Auto-Reset Maintenance Switch
By defining a template switch with automatic restore logic and a countdown script:
1. **Turn OFF**: The gateway deliberately disconnects from ZZK-SA2 and disables `auto_connect`, opening a 3-minute (180-second) advertising window for OTA flashing.
2. **Auto-Restore**: After the 180-second countdown expires, the switch automatically toggles back ON, re-enables `auto_connect`, and resumes normal gateway management.
3. **Power-Cycle Safety**: `restore_mode: ALWAYS_ON` ensures that if the ESP32 reboots, it always defaults to keeping the connection active.

```yaml
script:
  - id: script_zzk_ota_countdown
    mode: restart
    then:
      - delay: 180s
      - switch.turn_on: zzk_maintenance_switch

switch:
  - platform: template
    name: "ZZK BLE Connection"
    id: zzk_maintenance_switch
    icon: "mdi:bluetooth"
    optimistic: true
    restore_mode: ALWAYS_ON
    turn_on_action:
      - script.stop: script_zzk_ota_countdown
      - lambda: |-
          id(zzk_sa2)->set_auto_connect(true);
    turn_off_action:
      - lambda: |-
          id(zzk_sa2)->set_auto_connect(false);
          id(zzk_sa2)->disconnect();
      - script.execute: script_zzk_ota_countdown
```

---

## 4. Complete Configuration Snippet

Merge the following snippet directly into your existing ESPHome node YAML configuration (or create a dedicated node):

```yaml
esp32_ble_tracker:

# If bluetooth_proxy is enabled, disabling GATT service caching is recommended to discover new characteristics
bluetooth_proxy:
  active: true
  cache_services: false

# 1. BLE connection countdown maintenance script
script:
  - id: script_zzk_ota_countdown
    mode: restart
    then:
      - delay: 180s
      - switch.turn_on: zzk_maintenance_switch

# 2. BLE client core
ble_client:
  - mac_address: "C1:XX:XX:XX:XX:XX"    # Replace with your device's actual MAC address
    id: zzk_sa2
    auto_connect: true
    on_connect:
      then:
        - binary_sensor.template.publish:
            id: zzk_ble_connected
            state: ON
    on_disconnect:
      then:
        - binary_sensor.template.publish:
            id: zzk_ble_connected
            state: OFF

# 3. Entity definitions (connectivity and door sensor)
binary_sensor:
  - platform: template
    name: "ZZK Connected"
    id: zzk_ble_connected
    device_class: connectivity

  - platform: template
    name: "ZZK Door Sensor"
    id: zzk_door
    device_class: door

# 4. Sensor entities (battery level and raw binary sensor decoding)
sensor:
  - platform: ble_client
    ble_client_id: zzk_sa2
    name: "ZZK Battery"
    type: characteristic
    device_class: battery
    unit_of_measurement: "%"
    accuracy_decimals: 0
    service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
    characteristic_uuid: "2d220002-516b-47bd-a33b-2c93889ac9b7"
    notify: true
    update_interval: 300s
    force_update: true
    lambda: |-
      if (x.size() > 0) {
        return (float)x[0];
      }
      return {};

  - platform: ble_client
    ble_client_id: zzk_sa2
    id: zzk_binary_raw
    type: characteristic
    internal: true
    service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
    characteristic_uuid: "2d220003-516b-47bd-a33b-2c93889ac9b7"
    notify: true
    update_interval: 60s
    force_update: true
    lambda: |-
      if (!x.empty()) {
        uint8_t raw = x[0];
        // Bit 0 mapped to door sensor (true=Open, false=Closed)
        bool is_open = (raw & (1 << 0)) != 0;
        id(zzk_door).publish_state(is_open);
        return (float)raw;
      }
      return {};

# 5. Switch control and maintenance mode entities
switch:
  # Physical wall switch control
  - platform: template
    name: "ZZK Switch"
    optimistic: true
    turn_on_action:
      - ble_client.ble_write:
          id: zzk_sa2
          service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
          characteristic_uuid: "2d220001-516b-47bd-a33b-2c93889ac9b7"
          value: [0x01]
    turn_off_action:
      - ble_client.ble_write:
          id: zzk_sa2
          service_uuid: "2d220000-516b-47bd-a33b-2c93889ac9b7"
          characteristic_uuid: "2d220001-516b-47bd-a33b-2c93889ac9b7"
          value: [0x00]

  # Maintenance mode switch (turn OFF to release BLE connection for 180s for OTA)
  - platform: template
    name: "ZZK BLE Connection"
    id: zzk_maintenance_switch
    icon: "mdi:bluetooth"
    optimistic: true
    restore_mode: ALWAYS_ON
    turn_on_action:
      - script.stop: script_zzk_ota_countdown
      - lambda: |-
          id(zzk_sa2)->set_auto_connect(true);
    turn_off_action:
      - lambda: |-
          id(zzk_sa2)->set_auto_connect(false);
          id(zzk_sa2)->disconnect();
      - script.execute: script_zzk_ota_countdown
```
