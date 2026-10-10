#!/usr/bin/env python3
"""
ZZK Switch Assistant - Standalone BLE OTA Flasher
Automatically triggers DFU on ZZK-SA2, waits for XIAO-DFU, and transfers firmware.
"""

import sys
import os
import json
import zipfile
import struct
import asyncio
import argparse
from bleak import BleakScanner, BleakClient

# UUIDs for Nordic Legacy DFU (used by Adafruit / Seeed Bootloader)
UUID_DFU_SERVICE = "00001530-1212-efde-1523-785feabcd123"
UUID_DFU_CONTROL = "00001531-1212-efde-1523-785feabcd123"
UUID_DFU_PACKET  = "00001532-1212-efde-1523-785feabcd123"

# OpCodes
OP_START_DFU                   = 1
OP_INITIALIZE_DFU              = 2
OP_RECEIVE_FIRMWARE_IMAGE      = 3
OP_VALIDATE_FIRMWARE_IMAGE     = 4
OP_ACTIVATE_FIRMWARE_AND_RESET = 5
OP_REQ_PKT_RCPT_NOTIFICATION   = 8
OP_RESPONSE                    = 16

PKT_RECEIPT_NOTIF_INTERVAL = 10
DATA_PACKET_SIZE = 20


class LegacyDfuClient:
    def __init__(self, client: BleakClient):
        self.client = client
        self.response_future = None
        self.notification_event = asyncio.Event()

    def _notification_handler(self, sender, data: bytearray):
        opcode = data[0]
        if opcode == OP_RESPONSE:
            req_op = data[1]
            status = data[2]
            if self.response_future and not self.response_future.done():
                self.response_future.set_result((req_op, status))
        elif opcode == 17:  # PKT_RCPT_NOTIF
            self.notification_event.set()

    async def start(self):
        # Allow BlueZ GATT discovery and connection parameters to stabilize
        await asyncio.sleep(0.6)
        for attempt in range(3):
            try:
                await self.client.start_notify(UUID_DFU_CONTROL, self._notification_handler)
                return
            except Exception as e:
                if attempt == 2:
                    raise
                await asyncio.sleep(0.6)

    async def wait_response(self, expected_op: int, timeout=15.0):
        try:
            res_op, status = await asyncio.wait_for(self.response_future, timeout=timeout)
            if status != 1:  # 1 = SUCCESS
                raise RuntimeError(f"DFU opcode 0x{expected_op:02X} failed with error status: {status}")
            return res_op, status
        except asyncio.TimeoutError:
            raise TimeoutError(f"DFU opcode 0x{expected_op:02X} timed out waiting for response")

    async def send_control(self, opcode: int, data: bytes = b"", wait_for_response=True, timeout=15.0):
        if wait_for_response:
            self.response_future = asyncio.get_running_loop().create_future()
        
        payload = bytes([opcode]) + data
        await self.client.write_gatt_char(UUID_DFU_CONTROL, payload, response=True)
        
        if wait_for_response:
            return await self.wait_response(opcode, timeout)

    async def send_packet(self, data: bytes):
        await self.client.write_gatt_char(UUID_DFU_PACKET, data, response=False)

    async def flash_package(self, dat_bytes: bytes, bin_bytes: bytes):
        print(f"📦 Preparing transfer (Init size: {len(dat_bytes)}B, App size: {len(bin_bytes)}B)...")
        
        # 1. Start DFU (Application mode = 4)
        size_packet = struct.pack("<III", 0, 0, len(bin_bytes))
        print("➡️  Sending START DFU command & image size...")
        self.response_future = asyncio.get_running_loop().create_future()
        await self.client.write_gatt_char(UUID_DFU_CONTROL, bytes([OP_START_DFU, 4]), response=True)
        await self.send_packet(size_packet)
        await self.wait_response(OP_START_DFU, timeout=15.0)

        # 2. Init Packet
        print("➡️  Sending Init Packet...")
        # 0x02 + 0x00: Init Packet Start (No notification expected)
        await self.send_control(OP_INITIALIZE_DFU, bytes([0]), wait_for_response=False)
        for i in range(0, len(dat_bytes), DATA_PACKET_SIZE):
            chunk = dat_bytes[i:i + DATA_PACKET_SIZE]
            await self.send_packet(chunk)
        # 0x02 + 0x01: Init Packet Complete (Wait for notification)
        await self.send_control(OP_INITIALIZE_DFU, bytes([1]), wait_for_response=True, timeout=15.0)

        # 3. Set Packet Receipt Notification Interval
        await self.send_control(OP_REQ_PKT_RCPT_NOTIFICATION, struct.pack("<H", PKT_RECEIPT_NOTIF_INTERVAL), wait_for_response=False)

        # 4. Receive Firmware Image
        print("➡️  Sending Firmware Image...")
        self.response_future = asyncio.get_running_loop().create_future()
        await self.client.write_gatt_char(UUID_DFU_CONTROL, bytes([OP_RECEIVE_FIRMWARE_IMAGE]), response=True)

        total_bytes = len(bin_bytes)
        bytes_sent = 0
        pkt_count = 0

        for i in range(0, total_bytes, DATA_PACKET_SIZE):
            chunk = bin_bytes[i:i + DATA_PACKET_SIZE]
            await self.send_packet(chunk)
            bytes_sent += len(chunk)
            pkt_count += 1

            if pkt_count % PKT_RECEIPT_NOTIF_INTERVAL == 0:
                self.notification_event.clear()
                try:
                    await asyncio.wait_for(self.notification_event.wait(), timeout=15.0)
                except asyncio.TimeoutError:
                    raise TimeoutError(f"Lost notification ack around {bytes_sent}/{total_bytes} bytes")

            # Progress print
            percent = (bytes_sent / total_bytes) * 100
            sys.stdout.write(f"\r🚀 Progress: {percent:5.1f}% [{bytes_sent}/{total_bytes} bytes]")
            sys.stdout.flush()

        print("\n⏳ Waiting for image transfer confirmation...")
        await self.wait_response(OP_RECEIVE_FIRMWARE_IMAGE, timeout=25.0)

        # 5. Validate Firmware
        print("🔍 Validating firmware on device...")
        await self.send_control(OP_VALIDATE_FIRMWARE_IMAGE, wait_for_response=True, timeout=15.0)

        # 6. Activate and Reset
        print("🔄 Activating firmware & rebooting device...")
        await self.send_control(OP_ACTIVATE_FIRMWARE_AND_RESET, wait_for_response=False)
        print("✅ OTA Upgrade Complete successfully!")


async def trigger_dfu_from_app(target_device):
    """Connect to ZZK-SA2, enable notify on DFU Control, and tell BLEDfu to reboot into DFU mode."""
    print(f"🔗 Connecting to {target_device.name} ({target_device.address}) to trigger DFU...")
    async with BleakClient(target_device.address, timeout=12.0) as client:
        # Check services
        dfu_control_char = None
        for service in client.services:
            for char in service.characteristics:
                if char.uuid.lower() == UUID_DFU_CONTROL.lower():
                    dfu_control_char = char
                    break

        if not dfu_control_char:
            raise RuntimeError("Device does not have BLEDfu service enabled! Please flash firmware with BLEDfu enabled first.")

        def dummy_cb(sender, data):
            pass

        print("🔔 Enabling DFU Control notifications...")
        for attempt in range(3):
            try:
                await client.start_notify(UUID_DFU_CONTROL, dummy_cb)
                break
            except Exception:
                if attempt == 2:
                    raise
                await asyncio.sleep(0.5)
        await asyncio.sleep(0.3)

        print("⚡ Sending reboot to DFU Bootloader command...")
        try:
            await client.write_gatt_char(UUID_DFU_CONTROL, bytes([1]), response=True)
            await asyncio.sleep(0.5)
        except Exception:
            pass
    print("⏳ Device rebooted into Bootloader.")


def is_xiao_dfu_name(name: str) -> bool:
    if not name:
        return False
    # Strict matching to avoid connecting to unrelated Xiaomi / smart home devices!
    n = name.strip()
    return n.startswith("XIAO_DFU") or n.startswith("XIAO-DFU") or n.startswith("ADA_DFU") or n == "DfuTarg"


async def main():
    parser = argparse.ArgumentParser(description="Standalone OTA Flasher for ZZK-SA2 / XIAO BLE")
    parser.add_argument("-z", "--zip", default=".pio/build/xiaoble_adafruit/firmware.zip", help="Path to firmware.zip")
    parser.add_argument("-t", "--timeout", type=float, default=20.0, help="Scan timeout")
    args = parser.parse_args()

    zip_path = args.zip
    if not os.path.isfile(zip_path):
        print(f"❌ Error: Firmware file not found: {zip_path}")
        sys.exit(1)

    # Read zip
    print(f"📦 Extracting package: {zip_path}")
    with zipfile.ZipFile(zip_path, 'r') as zf:
        manifest = json.loads(zf.read("manifest.json"))
        app_info = manifest["manifest"]["application"]
        dat_bytes = zf.read(app_info["dat_file"])
        bin_bytes = zf.read(app_info["bin_file"])

    print("🔎 Scanning for target device (ZZK-SA2 or XIAO_DFU)...")
    device_app = None
    device_dfu = None

    devices = await BleakScanner.discover(timeout=args.timeout)
    for d in devices:
        name = d.name or ""
        if "ZZK-SA2" in name:
            device_app = d
        elif is_xiao_dfu_name(name):
            device_dfu = d

    # Step 1: Trigger DFU if running application
    if not device_dfu and device_app:
        await trigger_dfu_from_app(device_app)
        print("⏳ Waiting for XIAO_DFU to appear in advertising...")
        await asyncio.sleep(2.0)
        
        # Scan again for XIAO_DFU
        for _ in range(12):
            devices = await BleakScanner.discover(timeout=2.0)
            for d in devices:
                name = d.name or ""
                if is_xiao_dfu_name(name):
                    device_dfu = d
                    break
            if device_dfu:
                break
            await asyncio.sleep(0.5)

    if not device_dfu:
        print("❌ Could not find XIAO_DFU device! Please ensure the device is in DFU mode.")
        sys.exit(1)

    print(f"🎯 Found Bootloader: {device_dfu.name} ({device_dfu.address})")
    print(f"🔗 Connecting to Bootloader...")
    
    async with BleakClient(device_dfu.address, timeout=15.0) as client:
        dfu = LegacyDfuClient(client)
        await dfu.start()
        await dfu.flash_package(dat_bytes, bin_bytes)


if __name__ == "__main__":
    asyncio.run(main())
