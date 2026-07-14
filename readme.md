# ZZK Switch

The ZZK Switch is a battery-powered BLE actuator based on the Seeed Studio XIAO nRF52840 that uses a servo motor to physically operate an existing wall-mounted light switch.

## System Overview

The ZZK Switch and CCJ Gateway form a web-controlled smart light switch system developed for use in a student dormitory. The system uses a servo motor to physically operate an existing wall switch without modifying the room's electrical wiring.

The ZZK Switch operates as a BLE peripheral, while the CCJ Gateway operates as the BLE central device. When the switch receives `0x01`, it moves the servo to press the ON side of the wall switch. When it receives `0x00`, it moves the servo in the opposite direction to press the OFF side. After each operation, the servo returns to its neutral position.

To reduce power consumption, the 5 V boost converter and servo are activated only when a command is received. After the mechanical operation is completed, the servo is detached and the boost converter is disabled.

The ZZK Switch also measures the battery voltage through its ADC. It converts the measurement into an estimated battery percentage and sends the value to the CCJ Gateway through a BLE notification approximately every 10 seconds.

This repository contains the software for the ZZK Switch. The gateway-side software is available in the [CCJ Gateway repository](https://github.com/coauther/CCJ-Gateway).

## Responsibilities of the ZZK Switch

The ZZK Switch is responsible for:

- Advertising its BLE service for discovery by the CCJ Gateway
- Receiving one-byte ON and OFF commands through a BLE characteristic
- Enabling the 5 V boost converter before operating the servo
- Driving the servo to physically press the wall switch
- Returning the servo to its neutral position after each operation
- Disabling the servo and boost converter to reduce power consumption
- Measuring the battery voltage through the ADC
- Converting the ADC value into an estimated battery percentage
- Sending battery notifications approximately every 10 seconds