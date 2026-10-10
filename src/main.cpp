#include <Arduino.h>
#include <bluefruit.h>
#include <Servo.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.h.example"
#endif

// OTA DFU Service
static BLEDfu            bledfu;

static BLEService        switch_service = BLEService("2d220000-516b-47bd-a33b-2c93889ac9b7");
static BLECharacteristic switch_char    = BLECharacteristic("2d220001-516b-47bd-a33b-2c93889ac9b7");
static BLECharacteristic battery_char   = BLECharacteristic("2d220002-516b-47bd-a33b-2c93889ac9b7");

static Servo             servo;
static TimerHandle_t     battery_timer;

static void battery_update_level(void)
{
    // Multiple-sample averaging
    uint32_t adc_sum = 0;
    for (int i = 0; i < 8; i++) {
        adc_sum += analogRead(PIN_VBAT);
        delay(1);
    }
    int raw = adc_sum / 8;

    // Calculate actual battery voltage in millivolts
    // 2.4V internal reference, 10-bit resolution (1023), resistor divider (1000k + 510k) / 510k
    float pin_voltage_mv = (raw * 2400.0f) / 1023.0f;
    uint32_t vbat_mv = (uint32_t)(pin_voltage_mv * (1510.0f / 510.0f));

    // Map 3.3V (empty) - 4.2V (full) to 0 - 100%
    int percentage = map(vbat_mv, 3300, 4200, 0, 100);
    uint8_t battery_level = (uint8_t)constrain(percentage, 0, 100);

    // Update local characteristic value for read requests
    battery_char.write8(battery_level);

    // Notify connected central if notifications are enabled
    if (Bluefruit.connected() && battery_char.notifyEnabled()) {
        battery_char.notify8(battery_level);
    }
}

static void battery_timer_callback(TimerHandle_t p_timer)
{
    (void)p_timer;
    battery_update_level();
}

static void battery_cccd_callback(uint16_t conn_hdl, BLECharacteristic *p_chr, uint16_t cccd_value)
{
    (void)cccd_value;
    if (p_chr->notifyEnabled(conn_hdl)) {
        battery_update_level();
    }
}

static void switch_write_callback(uint16_t conn_hdl, BLECharacteristic *p_chr, uint8_t *p_data, uint16_t len)
{
    (void)conn_hdl;
    (void)p_chr;

    if (len < 1) {
        return;
    }

    uint8_t command = p_data[0];

    // Enable 5V boost converter
    digitalWrite(EN_PIN, LOW);

    // Wait for the capacitor to be charged
    delay(50);

    // Attach servo pin
    servo.attach(SERVO_PIN);

    if (command == 0x01) {
        // ON
        servo.write(SERVO_ANGLE_ON);
        delay(SERVO_HOLD_MS);
        servo.write(SERVO_ANGLE_NEUTRAL);
    } else if (command == 0x00) {
        // OFF
        servo.write(SERVO_ANGLE_OFF);
        delay(SERVO_HOLD_MS);
        servo.write(SERVO_ANGLE_NEUTRAL);
    }

    // Wait for the servo to execute
    delay(SERVO_COOLDOWN_MS);

    // Shutdown 5V boost converter and servo
    servo.detach();
    digitalWrite(EN_PIN, HIGH);
}

void setup(void)
{
    // Low-power configuration
    NRF_POWER->DCDCEN = 1; // Enable DCDC
    NRF_USBD->ENABLE = 0;  // Disable USB peripherals

    // Set ADC resolution and reference
    analogReadResolution(10);
    analogReference(AR_INTERNAL_2_4); // 2.4V internal reference

    // Configure battery measurement pins
    pinMode(PIN_VBAT, INPUT);
    pinMode(VBAT_ENABLE, OUTPUT);
    digitalWrite(VBAT_ENABLE, LOW);

    // Turn off all onboard RGB LEDs
    pinMode(LED_RED, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    pinMode(LED_GREEN, OUTPUT);
    digitalWrite(LED_GREEN, HIGH);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, HIGH);

    // Initialize 5V boost converter (turned off at first)
    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);

    Bluefruit.begin();
    // Disable automatic status LED blinking during advertising and connection
    Bluefruit.autoConnLed(false);

    // Set Bluetooth radio power (range: -40, -20, -16, -12, -8, -4, 0, 2, 3, 4, 5, 6, 7, 8)
    Bluefruit.setTxPower(0);
    Bluefruit.setName("ZZK-SA2");

    switch_service.begin();

    switch_char.setProperties(CHR_PROPS_WRITE);
    switch_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    switch_char.setWriteCallback(switch_write_callback);
    switch_char.begin();

    battery_char.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    battery_char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    battery_char.setFixedLen(1);
    battery_char.setCccdWriteCallback(battery_cccd_callback);
    battery_char.begin();

    // Initialize OTA DFU service
    bledfu.begin();

    // Read and initialize battery level characteristic
    battery_update_level();

    // Start periodic software timer for battery reporting
    battery_timer = xTimerCreate("vbat_timer", pdMS_TO_TICKS(BATTERY_CHECK_MS), pdTRUE, NULL, battery_timer_callback);
    if (battery_timer != NULL) {
        xTimerStart(battery_timer, 0);
    }

    // Main advertising pack (service uuid)
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(switch_service);

    // Scan Response (device name)
    Bluefruit.ScanResponse.addName();

    Bluefruit.Advertising.restartOnDisconnect(true);
    // Set advertising interval
    Bluefruit.Advertising.setInterval(320, 1600);
    Bluefruit.Advertising.start(0);

    // Suspend loop task since all tasks are event-driven
    suspendLoop();
}

void loop(void)
{
    // Loop task is suspended via suspendLoop() in setup()
}
