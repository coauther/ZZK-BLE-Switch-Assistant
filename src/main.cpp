#include <Arduino.h>
#include <bluefruit.h>
#include <Servo.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.h.example"
#endif

#ifndef BATTERY_CHECK_MS
#define BATTERY_CHECK_MS     300000
#endif

// Pin definitions fallback for battery sensing
#if !defined(PIN_VBAT_ENABLE) && defined(VBAT_ENABLE)
#define PIN_VBAT_ENABLE      VBAT_ENABLE
#endif

BLEService               switchService = BLEService("2d220000-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic switchChar    = BLECharacteristic("2d220001-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic batteryChar   = BLECharacteristic("2d220002-516b-47bd-a33b-2c93889ac9b7");

Servo zzkServo;
TimerHandle_t batteryTimer;

void updateBatteryLevel() {
    // Enable battery divider circuit (active low on XIAO BLE)
    digitalWrite(PIN_VBAT_ENABLE, LOW);
    delayMicroseconds(50);

    // Read ADC value
    int raw = analogRead(PIN_VBAT);

    // Disable battery divider circuit immediately to prevent parasitic battery drain
    digitalWrite(PIN_VBAT_ENABLE, HIGH);

    // Calculate actual battery voltage in millivolts
    // 2.4V internal reference, 10-bit resolution (1023), resistor divider (1000k + 510k) / 510k
    float pin_voltage_mv = (raw * 2400.0f) / 1023.0f;
    uint32_t vbat_mv = (uint32_t)(pin_voltage_mv * (1510.0f / 510.0f));

    // Map 3.3V (empty) - 4.2V (full) to 0 - 100%
    int percentage = map(vbat_mv, 3300, 4200, 0, 100);
    uint8_t battery_level = (uint8_t)constrain(percentage, 0, 100);

    // Update local characteristic value for read requests
    batteryChar.write8(battery_level);

    // Notify connected central if notifications are enabled
    if (Bluefruit.connected() && batteryChar.notifyEnabled()) {
        batteryChar.notify8(battery_level);
    }
}

void batteryTimerCallback(TimerHandle_t xTimer) {
    (void)xTimer;
    updateBatteryLevel();
}

void onBatteryCccdWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint16_t cccd_value) {
    (void)cccd_value;
    if (chr->notifyEnabled(conn_hdl)) {
        updateBatteryLevel();
    }
}

void onSwitchWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len < 1) return;

    uint8_t command = data[0];

    // Enable 5V boost converter
    digitalWrite(EN_PIN, LOW);

    // Wait for the capacitor to be charged
    delay(50);

    // Attach servo pin
    zzkServo.attach(SERVO_PIN);

    if (command == 0x01) {
        // ON
        zzkServo.write(SERVO_ANGLE_ON);
        delay(SERVO_HOLD_MS);
        zzkServo.write(SERVO_ANGLE_NEUTRAL);
    } else if (command == 0x00) {
        // OFF
        zzkServo.write(SERVO_ANGLE_OFF);
        delay(SERVO_HOLD_MS);
        zzkServo.write(SERVO_ANGLE_NEUTRAL);
    }

    // Wait for the servo to execute
    delay(SERVO_COOLDOWN_MS);

    // Shutdown 5V boost converter and servo
    zzkServo.detach();
    digitalWrite(EN_PIN, HIGH);
}

void setup() {
    // Low-power configuration
    NRF_POWER->DCDCEN = 1; // Enable DCDC
    NRF_USBD->ENABLE = 0;  // Disable USB peripherals

    // Set ADC resolution and reference
    analogReadResolution(10);
    analogReference(AR_INTERNAL_2_4); // 2.4V internal reference

    // Configure battery measurement pins
    pinMode(PIN_VBAT, INPUT);
    pinMode(PIN_VBAT_ENABLE, OUTPUT);
    digitalWrite(PIN_VBAT_ENABLE, HIGH); // Keep divider turned off initially to save power

    // Disable onboard LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    // Initialize 5V boost converter (turned off at first)
    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);

    Bluefruit.begin();
    // Set Bluetooth radio power (range: -40, -20, -16, -12, -8, -4, 0, 2, 3, 4, 5, 6, 7, 8)
    Bluefruit.setTxPower(0);
    Bluefruit.setName("ZZK-SA2");

    switchService.begin();

    switchChar.setProperties(CHR_PROPS_WRITE);
    switchChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    switchChar.setWriteCallback(onSwitchWrite);
    switchChar.begin();

    batteryChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    batteryChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    batteryChar.setFixedLen(1);
    batteryChar.setCccdWriteCallback(onBatteryCccdWrite);
    batteryChar.begin();

    // Read and initialize battery level characteristic
    updateBatteryLevel();

    // Start periodic software timer for battery reporting
    batteryTimer = xTimerCreate("vbat_timer", pdMS_TO_TICKS(BATTERY_CHECK_MS), pdTRUE, NULL, batteryTimerCallback);
    if (batteryTimer != NULL) {
        xTimerStart(batteryTimer, 0);
    }

    // Main advertising pack (service uuid)
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(switchService);

    // Scan Response (device name)
    Bluefruit.ScanResponse.addName();

    Bluefruit.Advertising.restartOnDisconnect(true);
    // Set advertising interval
    Bluefruit.Advertising.setInterval(320, 1600);
    Bluefruit.Advertising.start(0);

    // Suspend loop task since all tasks are event-driven
    suspendLoop();
}

void loop() {
    // Loop task is suspended via suspendLoop() in setup()
}
