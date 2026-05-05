#include <Arduino.h>
#include <bluefruit.h>

BLEService               switchService = BLEService("2d220000-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic switchChar    = BLECharacteristic("2d220001-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic batteryChar   = BLECharacteristic("2d220002-516b-47bd-a33b-2c93889ac9b7");

void onSwitchWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len > 0) {
        if (data[0] == 0x01) digitalWrite(LED_BUILTIN, LOW);
        else if (data[0] == 0x00) digitalWrite(LED_BUILTIN, HIGH);
    }
}

void setup() {
    // 1. 物理层省电：彻底切断 USB 和开启高效 DCDC
    NRF_POWER->DCDCEN = 1; // 开启 DCDC，大幅降低运行功耗 [cite: 177]
    NRF_USBD->ENABLE = 0;  // 彻底关掉 USB 外设，释放 HFCLK 时钟锁

    // 关闭 LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Bluefruit.begin();
    // 调低发射功率也能省电 (可选范围: -40, -20, -16, -12, -8, -4, 0, 2, 3, 4, 5, 6, 7, 8)
    Bluefruit.setTxPower(0);
    Bluefruit.setName("ZZK_Switch"); // 现在的空间足够放下完整名字了

    switchService.begin();

    switchChar.setProperties(CHR_PROPS_WRITE);
    switchChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    switchChar.setWriteCallback(onSwitchWrite);
    switchChar.begin();

    batteryChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    batteryChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    batteryChar.begin();
    batteryChar.write8(100);

    // 2. 优化广播策略：解决名字截断问题
    // 主广播包：只放 Flags 和 名字
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addName();

    // 副包 (Scan Response)：把笨重的 UUID 塞进这里
    Bluefruit.ScanResponse.addService(switchService);

    Bluefruit.Advertising.restartOnDisconnect(true);
    // 增加广播间隔 (比如 1秒/1600ms) 可以进一步省电 [cite: 138]
    Bluefruit.Advertising.setInterval(320, 1600);
    Bluefruit.Advertising.start(0);
}

 void loop() {
//     static uint32_t last_check = 0;
//
//     // 每 10 秒尝试读取并上报电量
//     if (millis() - last_check > 10000) {
//         last_check = millis();
//
//         // 读取 PIN_VBAT 处的真实电压（ADC 读取逻辑）
//         int raw = analogRead(PIN_VBAT);
//         int percentage = map(raw, 700, 890, 0, 100);
//         uint8_t battery_level = (uint8_t)constrain(percentage, 0, 100);
//
//         // 主动将电量推送（Notify）到网页端 [cite: 622]
//         batteryChar.notify8(battery_level);
//     }
//
//     delay(200); // 这里的延时会让 FreeRTOS 调度 CPU 进入浅睡 [cite: 157, 180]
 }
