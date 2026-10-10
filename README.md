# ZZK Switch Assistant 2 (ZZK-SA2)

**English** | [中文](README.zh-CN.md)

[Quick Start](#quick-start) · [Hardware](#hardware) · [BLE Interface](#ble-interface) · [OTA Update](#OTA Firmware Update) · [Known Limitations](#known-limitations)

> Turn off the lights without leaving your warm bed.

ZZK-SA2 is a battery-powered BLE wireless actuator that uses a servo motor to physically operate an existing wall switch. Optimized for low-power operation, with Home Assistant integration.

![ZZK-SA2 Assembled Prototype](docs/device.jpeg)

---

## Note

This is an experimental DIY project with rough edges. You may need to adapt or customize the firmware and hardware to make it work reliably with your specific setup.

---

## Introduction

ZZK-SA2 is built around the Seeed Studio XIAO nRF52840. It receives commands over Bluetooth Low Energy (BLE) and drives a servo motor to physically press a wall-mounted light switch, enabling remote lighting control without modifying the room's existing electrical wiring.

In a shared student dorm, turning off the lights often requires climbing down from the top bunk. Even in a private apartment, the only light switch may be located by the entrance—this is the exact inconvenience ZZK-SA2 was designed to eliminate.

The device exposes a standard BLE GATT interface and is independent of any particular home automation platform. Integration options include:
- **Home Assistant via ESPHome**: use an ESP32 board running ESPHome as a BLE gateway to bridge ZZK-SA2 into Home Assistant.
- **CCJ Gateway**: use the companion [CCJ Gateway repository](https://github.com/coauther/CCJ-Gateway) project to control the switch through a web interface.
- **Other BLE clients**: use any smartphone app or hardware capable of writing standard BLE GATT commands.

### Why the Name ZZK?

ZZK is the roommate of CCJ, one of the project's original authors. Since ZZK usually goes to bed later, the nightly duty of turning off the lights naturally fell to him. The ZZK Switch Assistant took over that job—and inherited his name.

### What Happened to ZZK-SA1?

The first-generation design used an ESP32 with ESP-NOW for communication. Prototype testing revealed that integration was cumbersome, standby power consumption was difficult to manage, and battery life was poor. That design was retired in favor of the nRF52840-based BLE architecture in ZZK-SA2.

### Advantages of ZZK-SA2

- **Optimized for low-power operation**: long battery life without an external power supply.
- **Standard BLE protocol**: seamless integration into existing smart home setups.
- **Modern wireless maintenance**: supports buttonless wireless OTA firmware updates.
- **Extensible architecture**: open design ready for future home automation enhancements.

---

## Demo

### Wall Switch Actuation

![ZZK-SA2 operating a wall switch](docs/switch-demo.gif)

### User Interfaces

#### Home Assistant Integration via ESPHome

When bridged through an ESPHome gateway, the switch control, battery percentage, connectivity status, and OTA maintenance mode switch map directly to Home Assistant entities, coexisting seamlessly with other sensors:

![Home Assistant entity page](docs/ha-entities.png)

#### CCJ Gateway Web Interface

When using a standalone ESP32 running the CCJ Gateway, a local web interface provides ON/OFF control and battery monitoring:

![CCJ Gateway Web Interface](docs/web-interface.png)

---

## Hardware

### Bill of Materials

| Category | Model / Specification | Quantity |
|---|---|---|
| Development board | Seeed Studio XIAO nRF52840 | 1 |
| Actuator | SG90 9g micro servo | 1 |
| Boost converter | 5 V DC-DC boost converter **with EN pin** | 1 |
| Battery | 3.7 V Li-ion battery (requires 1S protection circuit) | 1 |
| Enclosure / Mount | Custom 3D printed parts | 1 set |

### Pinout and Wiring

#### External Wiring

| Function | Pin | Level / Parameters |
|---|---|---|
| Boost converter enable | `D2` | Active LOW |
| Servo control signal | `D3` | PWM (50 Hz) |
| Servo power supply | — | 5 V |
| System power input | `BAT+` / `GND` | 3.3 V ~ 4.2 V |

For complete wiring details, refer to the [wiring schematic](docs/wiring-schematic.pdf): [![Wiring Schematic](docs/wiring-schematic.png)](docs/wiring-schematic.pdf) 

#### On-board Circuitry

The battery sensing circuit is fully integrated into the Seeed Studio XIAO nRF52840 on-board hardware, requiring no external wiring (refer to the [Seeed official wiki](https://wiki.seeedstudio.com/XIAO_BLE/)):
- `PIN_VBAT` (P0.31 / AIN7): connected to the on-board 1.5 MΩ (1000 kΩ + 510 kΩ) divider network.
- `PIN_VBAT_ENABLE` (P0.14): on-board voltage divider enable pin (Active LOW).

---

## BLE Interface

### Advertising and Identity

| Parameter | Value | Notes |
|---|---|---|
| Device Name | `ZZK-SA2` | Broadcast in Scan Response |
| Advertising Interval | 200 ~ 1000 ms | Fast advertising 200 ms, falling back to 1000 ms slow advertising |
| TX Power | 0 dBm | Balanced nRF52840 default |
| Device Role | BLE Peripheral | Single-peripheral mode; stops advertising once connected |

### GATT Services and Characteristics

| Service / Characteristic | UUID | Properties | Data Format | Description |
|---|---|---|---|---|
| **Switch Service** | `2d220000-516b-47bd-a33b-2c93889ac9b7` | — | — | Custom primary switch service |
| └─ Switch Control | `2d220001-516b-47bd-a33b-2c93889ac9b7` | Write | `uint8` | Write `0x01` to turn ON, `0x00` to turn OFF; unacknowledged |
| └─ Battery Level | `2d220002-516b-47bd-a33b-2c93889ac9b7` | Read, Notify | `uint8` | Remaining battery percentage (`0~100`) |
| **DFU Service** | `0000fe59-0000-1000-8000-00805f9b34fb` | — | — | Nordic Secure DFU OTA service |

Both custom characteristics have open read/write permissions, requiring no pairing or link encryption.

### Battery Measurement and Reporting Strategy

- **Sampling and filtering**: 10-bit SAADC with a 2.4 V internal reference; 8-sample moving average filter eliminates instantaneous voltage fluctuations.
- **Voltage mapping**: calculates real battery millivolts using the on-board divider ratio and applies linear mapping over 3.3 V (0%) ~ 4.2 V (100%).
- **Dual-trigger reporting**:
  - **Immediate notification on connection**: writing to the CCCD triggers an immediate push of the current battery level without waiting for the timer.
  - **Periodic reporting**: a FreeRTOS software timer samples and notifies every 5 minutes.

---

## Low-Power Design

The device is powered by a single 3.7 V Li-ion battery. To achieve months of battery life, the firmware implements multiple power-saving optimizations across hardware peripherals and FreeRTOS task scheduling:

| Mechanism | Configuration / Strategy | Description |
|---|---|---|
| Dynamic servo power | Hardware enable gating | Keeps `D2` HIGH to shut down the 5 V boost converter, eliminating quiescent leakage from the servo and boost module |
| FreeRTOS scheduling | `suspendLoop()` | Suspends the main loop task after `setup()`, eliminating CPU polling and switching entirely to event-driven execution |
| Scheduled tasks | FreeRTOS software timer | Wakes the MCU only during battery sampling intervals (default 5 minutes), immediately returning to sleep |
| Regulator mode | Internal DC-DC regulator enabled | Sets `NRF_POWER->DCDCEN`, significantly reducing standby and radio current compared to default LDO mode |
| Peripheral shutdown | USB controller disabled | Clears `NRF_USBD->ENABLE`, turning off USB clocks and internal detection |
| LED shutdown | All on-board LEDs disabled | Drives RGB LEDs HIGH and calls `Bluefruit.autoConnLed(false)` to disable BLE connection and advertising blinks |

---

## Quick Start

### Requirements

- [PlatformIO Core](https://platformio.org/) or PlatformIO IDE (VS Code / CLion plugin)
- Platform: community-maintained `maxgerhardt/platform-nordicnrf52` branch, board `xiaoble_adafruit`

### Hardware Configuration

The firmware decouples hardware pin and angle parameters from source code. After cloning the repository, copy the example configuration file:

```bash
cp src/config.h.example src/config.h
```

`src/config.h` is ignored by `.gitignore`, allowing local parameter tuning:

| Parameter | Default | Description |
|---|---|---|
| `EN_PIN` | `D2` | 5 V boost converter enable pin (Active LOW) |
| `SERVO_PIN` | `D3` | Servo PWM control signal pin |
| `SERVO_ANGLE_ON` | `130` | Servo angle for turning light ON (degrees) |
| `SERVO_ANGLE_OFF` | `40` | Servo angle for turning light OFF (degrees) |
| `SERVO_ANGLE_NEUTRAL` | `90` | Neutral resting angle after actuation (degrees) |
| `SERVO_HOLD_MS` | `500` | Hold duration at the pressed position (ms) |
| `SERVO_COOLDOWN_MS` | `500` | Cooldown buffer after returning to neutral before power gating (ms) |
| `BATTERY_CHECK_MS` | `300000` | Battery sampling and reporting interval (ms, default 5 min) |

### Build and Initial Flashing

The initial firmware flash must be performed over USB:

```bash
# 1. Compile
pio run

# 2. Flash (double-tap RESET on the board to enter bootloader mode, wait for the virtual drive to appear)
pio run -t upload

# 3. Serial monitor (115200 baud)
pio device monitor
```

---

## OTA Firmware Update

The firmware integrates Nordic Secure DFU. Once flashed initially over USB, subsequent updates can be deployed wirelessly over BLE.

> [!WARNING]
> Some Seeed XIAO BLE boards ship with a factory bootloader bug that causes a lockup during BLE OTA. You **must** flash the community-fixed bootloader before attempting OTA updates. Refer to the [OTA Firmware Update Guide](docs/ota-guide.md) for detailed recovery steps and workflows.

### One-Click Wireless Flashing (Recommended)

Before flashing over OTA, ensure no other central devices (apps or gateways) are connected to ZZK-SA2.

Install the `bleak` Python library (`pip install bleak`), then run the automated script from the project root:

```bash
python3 ota_flash.py
```

The script automatically scans for `ZZK-SA2`, triggers a reboot into DFU mode, captures the `XIAO_DFU` advertising, uploads the firmware package, and reboots the device (takes ~30–60 seconds).

---

## Gateway Integration and Usage

### 1. Home Assistant via ESPHome (Recommended)

Use an ESP32 running ESPHome as a BLE gateway to bridge the device into Home Assistant.

> [!TIP]
> For complete YAML configuration including battery sensors and a 180-second maintenance mode switch to release the BLE connection for OTA, see the [ESPHome Integration Guide](docs/esphome-guide.md).

### 2. CCJ Gateway (Alternative)

If Home Assistant is not available, flash the companion [CCJ Gateway](https://github.com/coauther/CCJ-Gateway) to an ESP32 for browser-based control.

### 3. Web Bluetooth Debug Page

Open [`docs/index.html`](docs/index.html) in Chrome or Edge over HTTPS or `http://localhost` to test and debug controls directly without a gateway.

### 4. Mobile App Debugging via nRF Connect

Install the Nordic nRF Connect app on your mobile device to discover the device and manually write characteristic values for quick troubleshooting.

---

## Known Limitations

- **Mechanical customization required**: servo angles and 3D printed mounting geometry must be adapted for specific wall switch designs.
- **No physical state feedback**: sends open-loop push/pull commands without detecting whether the light is physically ON or OFF.
- **Battery estimation accuracy**: uses linear voltage mapping rather than a fitted Li-ion discharge curve.
- **Communication security**: GATT characteristics do not currently enforce authentication or link encryption.

---

## Authors and Contributors

- **[CHENJIE CAO](https://github.com/coauther)** — Original Author
- **[Zichen Yao](https://github.com/Zi03080514)** — Maintainer / Core Contributor

---

## Acknowledgments

- Companion Gateway: [CCJ Gateway](https://github.com/coauther/CCJ-Gateway)
- Firmware libraries: [Adafruit Bluefruit nRF52](https://github.com/adafruit/Adafruit_nRF52_Arduino) and `Servo` library
- Bootloader fix: [oltaco/Adafruit_nRF52_Bootloader_OTAFIX](https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX)

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

You are free to use, modify, and distribute the code, provided the original copyright notice and license text are preserved.
