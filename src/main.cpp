#include <Arduino.h>
#include <bluefruit.h>
#include <Servo.h>

#define EN_PIN D2       // 控制 5V 升压模块的使能引脚 (建议选 D2)
#define SERVO_PIN D3    // 控制舵机 PWM 信号的引脚

BLEService               switchService = BLEService("2d220000-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic switchChar    = BLECharacteristic("2d220001-516b-47bd-a33b-2c93889ac9b7");
BLECharacteristic batteryChar   = BLECharacteristic("2d220002-516b-47bd-a33b-2c93889ac9b7");

Servo zzkServo;         // 创建舵机对象

void onSwitchWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len < 1) return;

    uint8_t command = data[0];

    // 1. 激活升压模块电源
    digitalWrite(EN_PIN, LOW);

    // 2. 充电缓冲延迟 (等待 5V 电压在电容中稳定，极度重要！)
    delay(50);

    // 3. 连接舵机信号线
    zzkServo.attach(SERVO_PIN);

    if (command == 0x01) {
        // 收到开灯指令 (例如：舵机转动到 90 度去按压开关)
        zzkServo.write(20);
        delay(500);
        zzkServo.write(90);
    } else if (command == 0x00) {
        // 收到关灯指令 (例如：舵机转回 0 度复位)
        zzkServo.write(120);
        delay(500);
        zzkServo.write(90);
    }

    // 4. 机械动作延迟 (给舵机足够的物理转动时间)
    delay(500);

    // 5. 切断外设信号与电源，回归微功耗
    zzkServo.detach();           // 先断开 PWM 信号，防乱抖
    digitalWrite(EN_PIN, HIGH);   // 彻底关闭 5V 升压模块
}

void setup() {
    // 1. 物理层省电：彻底切断 USB 和开启高效 DCDC
    NRF_POWER->DCDCEN = 1; // 开启 DCDC，大幅降低运行功耗 [cite: 177]
    NRF_USBD->ENABLE = 0;  // 彻底关掉 USB 外设，释放 HFCLK 时钟锁

    // 2. ADC 分辨率配置 (关键新增)
    // 将 ADC 分辨率设置为 10 位 (0-1023)，与你之前测出的 raw 值匹配 [cite: 246]
    analogReadResolution(10);

    // 关闭 LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH); // 确保开机时升压模块是关闭的，防止通电乱转

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
    batteryChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    batteryChar.setFixedLen(1);
    batteryChar.begin();

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
     static uint32_t last_check = 0;

     // 每 10 秒尝试读取并上报电量
     if (millis() - last_check > 10000) {
         last_check = millis();

         // 读取 PIN_VBAT 处的真实电压（ADC 读取逻辑）
         int raw = analogRead(PIN_VBAT);

         // 将 10 位 ADC 的裸数据映射为 0-100 的百分比
         // 提示：700 到 890 这个区间是基于标准 3.7V 锂电池放电曲线的经验值
         int percentage = map(raw, 700, 890, 0, 100);

         // 限制数值在 0-100 之间，防止过充或过放时数据溢出 [cite: 57]
         uint8_t battery_level = (uint8_t)constrain(percentage, 0, 100);

         // 主动将电量推送（Notify）给已连接的网关或网页 [cite: 57]
         if (Bluefruit.connected()) {
             batteryChar.notify8(battery_level);
         }
     }

    // 呼叫 RTOS 调度器让出 CPU，进入休眠 [cite: 58]
    delay(100);
 }
