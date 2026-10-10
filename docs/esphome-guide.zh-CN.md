# ESPHome / Home Assistant 集成指南

[English](esphome-guide.md) | **中文**

将 ZZK-SA2 接入 Home Assistant 最推荐的方式是使用一块常供电的 ESP32 开发板运行 [ESPHome](https://esphome.io/) 作为 BLE 网关。

本指南提供了完整的配置说明，包括物理开关实体、电量实体、在线状态监测，以及 180 秒自动倒计时 OTA 维护模式。

---

## 1. 准备工作：获取设备 MAC 地址

你可以通过以下任意方式获取 ZZK-SA2 的蓝牙 MAC 地址：

1. **手机 App 扫描**：在手机上使用 **nRF Connect for Mobile** 等 BLE 调试工具，扫描设备列表找到 `ZZK-SA2`，记录其 MAC 地址（如 `C1:23:45:67:89:AB`）。
2. **ESPHome 日志扫描**：在 ESP32 配置中开启 `esp32_ble_tracker:` 并烧录，在控制台日志中查看捕获到的 `[ZZK-SA2]` 广播与对应 MAC 地址。

---

## 2. 功能配置

### A. 蓝牙客户端与连接事件 (`ble_client`)
使用 `ble_client` 建立与 ZZK-SA2 的常驻连接，并通过 `on_connect` 与 `on_disconnect` 回调驱动在线状态实体：

```yaml
esp32_ble_tracker:

ble_client:
  - mac_address: "C1:XX:XX:XX:XX:XX"    # 替换为你设备的真实 MAC 地址
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

### B. 在线状态 (`binary_sensor`)
在 Home Assistant 中直观展示设备当前的连接连通性（`device_class: connectivity`）：

```yaml
binary_sensor:
  - platform: template
    name: "ZZK Connected"
    id: zzk_ble_connected
    device_class: connectivity
```

### C. 物理开关实体 (`switch`)
向自定义开关特征值写入 1 字节（`0x01` 开灯 / `0x00` 关灯）：

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
> **必须设置 `optimistic: true`**：由于 ZZK-SA2 是无状态的单向物理推拉机构，固件不会返回执行结果与物理开关的实际开闭状态。开启此项可避免 Home Assistant 前端开关在操作后被回弹复位。

### D. 电池电量传感器 (`sensor`)
订阅电量特征值的 Notify 通知，上报 `0~100` 的剩余百分比：

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

---

## 3. 防抢占维护模式

### 为什么需要维护模式？
ZZK-SA2 的主控 nRF52840 采用**单从机连接**模式：当 ESP32 网关连接上设备后，设备出于省电和协议规范会**立即停止向外广播**。  
此时，如果开发者想使用电脑脚本 `ota_flash.py` 或手机 App 升级固件，会发现无法搜索到设备。

### 解决方案：180 秒自动倒计时维护开关
通过在 ESPHome 中定义一个带有自动恢复逻辑的维护开关和一个延时恢复脚本：
1. **关闭开关**：网关主动断开与 ZZK-SA2 的连接，并关闭 `auto_connect`，为电脑或手机预留 3 分钟（180 秒）的 OTA 升级广播窗口。
2. **自动恢复**：后台倒计时 180 秒结束后，开关自动切回打开状态并重新开启 `auto_connect`，恢复对设备的接管。
3. **掉电防误触**：设置 `restore_mode: ALWAYS_ON`，确保网关重启后始终默认维持正常连接。

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

## 4. 完整的配置代码片段

将以下配置片段完整合并到你现有的 ESPHome 节点 YAML 文件中（或新建节点使用）：

```yaml
esp32_ble_tracker:

# 1. 蓝牙连接倒计时维护脚本
script:
  - id: script_zzk_ota_countdown
    mode: restart
    then:
      - delay: 180s
      - switch.turn_on: zzk_maintenance_switch

# 2. BLE 客户端核心
ble_client:
  - mac_address: "C1:XX:XX:XX:XX:XX"    # 替换为你设备的真实 MAC 地址
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

# 3. 在线状态实体
binary_sensor:
  - platform: template
    name: "ZZK Connected"
    id: zzk_ble_connected
    device_class: connectivity

# 4. 电量传感器实体
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

# 5. 开关控制与维护模式实体
switch:
  # 物理开关控制
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

  # 维护模式开关（关闭即释放蓝牙连接 180 秒用于 OTA 升级）
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
