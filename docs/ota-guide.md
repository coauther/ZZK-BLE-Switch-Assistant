# OTA Firmware Update Guide

**English** | [中文](ota-guide.zh-CN.md)

The ZZK-SA2 firmware integrates Nordic's official Secure DFU (Device Firmware Update) BLE service. After performing the initial flash over a USB cable, you can completely disconnect the wire and deploy future firmware updates wirelessly from a PC terminal or a mobile app.

---

## 1. Critical Prerequisite: Upgrading the Bootloader

> [!WARNING]
> **Known Bug in Factory Seeed XIAO BLE Bootloader** 
> The early factory bootloader flashed onto XIAO nRF52840 boards by Seeed Studio contains a critical bug: receiving BLE OTA firmware packets causes the MCU to lock up and lose its existing user firmware, requiring a wired USB connection to recover. For background details and community reproduction reports, refer to the [Seeed Studio Forum discussion](https://forum.seeedstudio.com/t/xiao-ble-sense-ota-dfu-from-arduino-ide-code/292567). 
> **Before attempting any BLE OTA updates, you must flash the community-fixed bootloader.**

### Step A: Download the Fixed Bootloader
Visit the community fix repository [oltaco/Adafruit_nRF52_Bootloader_OTAFIX Releases](https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX/releases) and download the latest update package:
- File name: `update-xiao_nrf52840_ble_sense_bootloader-0.9.2-OTAFIX2.3-BP1.4_nosd.uf2`

> [!IMPORTANT]
> **Note**: Even if your board is the standard XIAO BLE (without on-board IMU or microphone), **you may still need to download the file containing `_sense_` in its name**. Match the name of the virtual drive that mounts in the next step. If a mismatched binary is copied, the device will reject the update.

### Step B: Flash the UF2 Bootloader File
1. Connect the XIAO nRF52840 to your computer using a USB Type-C cable.
2. Quickly **double-tap** the on-board Reset button.
3. A virtual mass-storage drive named `XIAO-SENSE` will appear on your computer.
4. Drag and drop the downloaded `.uf2` file directly into the root of this virtual drive.
5. Once copying completes, the virtual drive automatically unmounts, the MCU reboots, and the on-board LED **flashes red rapidly**, indicating that the bootloader upgrade is complete.

> [!IMPORTANT]
> **Verification**: If the on-board LED does not flash red, the update may not have completed. To verify, check the `INFO_UF2.TXT` file in the virtual drive and confirm whether its build date matches the release date of the downloaded UF2 firmware.

### Step C: Flash the Base Firmware
After the bootloader upgrade is complete, perform a standard wired upload over USB to flash the latest firmware containing the `BLEDfu` service:

```bash
pio run -t upload
```

You can now disconnect the USB cable. All future development and updates can be conducted over wireless OTA.

---

## 2. Firmware Package Generation

Each time you compile the project with PlatformIO:

```bash
pio run
```

The build system generates the binary image as well as a standardized Nordic DFU update archive at:
```text
.pio/build/xiaoble_adafruit/firmware.zip
```

---

## 3. Method 1: One-Click Wireless Flashing via PC Terminal

The project root provides a cross-platform automation script: [`ota_flash.py`](../ota_flash.py).

### Environment Setup
Requires the cross-platform Python BLE library `bleak`:

```bash
pip install bleak
```
The script runs on Linux, Windows 10/11, and macOS using the built-in Bluetooth controller, with no specialized BLE dongles or sniffers required.

### Flashing
Run the script from the project root:

```bash
python3 ota_flash.py
```

### Automated Workflow
1. **Device discovery**: scans for nearby BLE advertisements and locates `ZZK-SA2`.
2. **Bootloader trigger**: connects and writes a trigger command to the DFU control characteristic, requesting a soft reboot into Bootloader DFU mode.
3. **Capture DFU advertising**: once rebooted, the device advertises as `XIAO_DFU`, and the script automatically reconnects.
4. **Packet transfer and verification**: streams the Init Packet and Firmware Image from `firmware.zip`, displaying a live progress bar.
5. **Reboot**: after verification succeeds, the chip reboots into the new firmware. The entire process takes approximately 30–60 seconds.

> [!TIP]
> If the device is already in `XIAO_DFU` bootloader mode, running the script directly will still automatically recognize it and complete the flash.

---

## 4. Method 2: Mobile App Flashing

If a PC terminal is unavailable, you can perform wireless updates using Nordic's official mobile apps.

### Prerequisites
- Install **nRF Connect for Mobile** or **nRF Device Firmware Update (DFU)** on Android / iOS.
- Transfer the compiled `.pio/build/xiaoble_adafruit/firmware.zip` package to your phone.

### Step-by-Step Instructions
1. Enable Bluetooth on your phone and open the **nRF Connect** app.
2. Scan for nearby devices, find `ZZK-SA2`, and tap **Connect**.
3. Once connected, tap the **DFU** icon in the top right corner.
4. Select **Distribution packet (ZIP)** as the file type, and choose the transferred `firmware.zip` from your file manager.
5. Tap **OK** / **Start** to initiate the update.

> [!IMPORTANT]
> At this point, the device will reboot and flash a red LED (entering DFU mode), but the update **may not proceed automatically**. You may see the DFU progress stall on package upload or display a timeout error.  
>  
> This is expected behavior: when the XIAO BLE enters DFU mode, both its BLE device name and its MAC address change, causing the app to lose track of the device. If this happens, exit the current screen, scan for devices again, connect to the device named `XIAO_DFU`, and run the DFU process once more to complete the upload.

6. Once the progress bar reaches 100%, the device automatically reboots into the updated firmware.
