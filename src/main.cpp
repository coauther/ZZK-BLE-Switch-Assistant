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
#if ENABLE_BINARY_SENSORS
static BLECharacteristic binary_sensor_char = BLECharacteristic("2d220003-516b-47bd-a33b-2c93889ac9b7");
#endif

static Servo             servo;
static TimerHandle_t     battery_timer;
#if ENABLE_BINARY_SENSORS
static TimerHandle_t     sensor_debounce_timer;
static volatile uint8_t  current_binary_state = 0;
#endif

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

#if ENABLE_BINARY_SENSORS
static uint8_t binary_sensor_read_state(void)
{
    uint8_t state = 0;
    for (uint8_t i = 0; i < NUM_BINARY_SENSORS; i++) {
        int val = digitalRead(BINARY_SENSORS[i].pin);
        bool is_active = BINARY_SENSORS[i].active_low ? (val == LOW) : (val == HIGH);
        if (is_active) {
            state |= (1 << i);
        }
    }
    return state;
}

static void binary_sensor_update(bool force_notify = false)
{
    uint8_t state = binary_sensor_read_state();
    if (force_notify || (state != current_binary_state)) {
        current_binary_state = state;
        binary_sensor_char.write8(state);
        if (Bluefruit.connected() && binary_sensor_char.notifyEnabled()) {
            binary_sensor_char.notify8(state);
        }
    }
}

static void sensor_debounce_timer_callback(TimerHandle_t p_timer)
{
    (void)p_timer;
    binary_sensor_update(false);
}

static void sensor_isr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (sensor_debounce_timer != NULL) {
        xTimerResetFromISR(sensor_debounce_timer, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

static void binary_sensor_cccd_callback(uint16_t conn_hdl, BLECharacteristic *p_chr, uint16_t cccd_value)
{
    (void)cccd_value;
    if (p_chr->notifyEnabled(conn_hdl)) {
        binary_sensor_update(true);
    }
}
#endif

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

    // Set device name with last 4 MAC digits (e.g. ZZK-SA2-A1B2)
    ble_gap_addr_t mac = Bluefruit.getAddr();
    char dev_name[16];
    snprintf(dev_name, sizeof(dev_name), "ZZK-SA2-%02X%02X", mac.addr[1], mac.addr[0]);
    Bluefruit.setName(dev_name);

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

#if ENABLE_BINARY_SENSORS
    binary_sensor_char.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    binary_sensor_char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    binary_sensor_char.setFixedLen(1);
    binary_sensor_char.setCccdWriteCallback(binary_sensor_cccd_callback);
    binary_sensor_char.begin();

    // Initialize one-shot software debounce timer
    sensor_debounce_timer = xTimerCreate("deb_timer", pdMS_TO_TICKS(SENSOR_DEBOUNCE_MS), pdFALSE, NULL, sensor_debounce_timer_callback);

    // Configure pins and attach wake-up interrupts
    for (uint8_t i = 0; i < NUM_BINARY_SENSORS; i++) {
        pinMode(BINARY_SENSORS[i].pin, BINARY_SENSORS[i].pin_mode);
        attachInterrupt(digitalPinToInterrupt(BINARY_SENSORS[i].pin), sensor_isr, CHANGE);
    }

    // Read and initialize binary sensor characteristic value
    current_binary_state = binary_sensor_read_state();
    binary_sensor_char.write8(current_binary_state);
#endif

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
