#include <Arduino.h>
#include <bluefruit.h>
#include <Servo.h>

#define EN_PIN D2       // Enable (EN) pin of the 5V boost converter
#define SERVO_PIN D3    // PWM pin of the servo motor

BLEService               switchService = BLEService("2d220000-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic switchChar    = BLECharacteristic("2d220001-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic batteryChar   = BLECharacteristic("2d220002-516b-47bd-a33b-2c93889ac9b7");

Servo zzkServo;

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
        zzkServo.write(20);
        delay(500);
        zzkServo.write(90);
    } else if (command == 0x00) {
        // OFF
        zzkServo.write(120);
        delay(500);
        zzkServo.write(90);
    }

    // Wait for the servo to execute
    delay(500);

    // Shutdown 5V boost converter and servo
    zzkServo.detach();
    digitalWrite(EN_PIN, HIGH);
}

void setup() {
    // Low-power configuration
    NRF_POWER->DCDCEN = 1; // Enable DCDC
    NRF_USBD->ENABLE = 0;  // Disable USB peripherals

    // Set ADC resolution
    analogReadResolution(10);

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
    batteryChar.begin();

    // Main advertising pack (service uuid)
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(switchService);

    // Scan Response (device name)
    Bluefruit.ScanResponse.addName();

    Bluefruit.Advertising.restartOnDisconnect(true);
    // Set advertising interval
    Bluefruit.Advertising.setInterval(320, 1600);
    Bluefruit.Advertising.start(0);
}

 void loop() {
     static uint32_t last_check = 0;

     // Report battery level (per 10 seconds)
     if (millis() - last_check > 10000) {
         last_check = millis();

         // ADC read
         int raw = analogRead(PIN_VBAT);

         // Map ADC value to percentage
         int percentage = map(raw, 700, 890, 0, 100);
         uint8_t battery_level = (uint8_t)constrain(percentage, 0, 100);

         // Notify the battery level
         if (Bluefruit.connected()) {
             batteryChar.notify8(battery_level);
         }
     }

    // Delay for system sleep
    delay(100);
 }
