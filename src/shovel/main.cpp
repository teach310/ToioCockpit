#include <M5Unified.h>
#include <ESP32Servo.h>
#include <NimBLEDevice.h>
#include <ShovelProtocol.hpp>

// サーボ (Servo Kit 180°) は AtomS3 本体の Grove 端子の G2 につなぐ
constexpr uint8_t SERVO_PIN = 2;
constexpr uint16_t SERVO_PULSE_MIN_US = 500;  // サーボが受け付けるパルス幅の範囲
constexpr uint16_t SERVO_PULSE_MAX_US = 2500;

// アームの位置 0 と 100 に対応するパルス幅。向きが逆なら入れ替える
constexpr uint16_t ARM_DOWN_US = 1000;
constexpr uint16_t ARM_UP_US = 2000;

// 目標の位置は cockpit から50msごとに少しずつ届く。そのまま追うと届くたびに止まってギクシャクするので、
// 目標との差に応じてなめらかに近づける（一次遅れ）。速すぎてtoioが揺れないよう、速さの上限も付ける
constexpr float SERVO_SMOOTH_MS = 80;           // 目標との差がおよそこの時間で縮まる
constexpr float SERVO_MAX_SPEED_US_PER_MS = 2.5f; // 速さの上限。端から端(1000μs)まで約0.4秒

// ATOM Mate for toio の電池。電圧を100kΩ2本で半分にして、5ピンのピン列の5番目（AtomS3では G8）に出している
// （ATOM Mate の回路図では ATOM Lite の GPIO33。AtomS3 の同じ位置が G8）
constexpr uint8_t BATTERY_PIN = 8;
constexpr uint32_t BATTERY_READ_INTERVAL_MS = 1000;
constexpr float BATTERY_SMOOTHING = 0.2f; // サーボが動くと電圧が揺れるので、読むたびに少しずつならす
// 5段階の境目 (mV)。リチウムポリマー電池のおおよその値。これらより低ければレベル1
constexpr int BATTERY_LEVEL_MV[] = {3650, 3750, 3850, 4000}; // この電圧以上でレベル 2, 3, 4, 5
// 充電中の推定。充電を知らせる信号はATOMのピンにつながっていないので、電圧の上がり方で決める
constexpr uint32_t CHARGE_CHECK_INTERVAL_MS = 20000; // この間隔で電圧を比べる
constexpr int CHARGE_RISE_MV = 15;                   // これだけ上がっていたら充電中
constexpr int CHARGE_FALL_MV = 15;                   // これだけ下がっていたら充電していない
constexpr int CHARGE_FULL_MV = 4150;                 // これ以上で安定していたら充電中（満充電近く）

float batteryMv = 0;
bool charging = false;
uint32_t lastBatteryReadMs = 0;
uint32_t lastChargeCheckMs = 0;
float lastChargeCheckMv = 0;

Servo servo;
float currentPulseUs = ARM_DOWN_US;
uint16_t writtenPulseUs = ARM_DOWN_US;
uint32_t lastUpdateUs = 0;

// BLE のコールバックは別のタスクから呼ばれるので volatile にする
volatile uint8_t targetArm = ShovelProtocol::ARM_MIN;
volatile bool connected = false;

NimBLECharacteristic* armCharacteristic = nullptr;

// 自動スリープ。cockpit とつながっていない間に触らなければ、画面を暗くし、さらにスリープに入る
constexpr uint32_t DIM_AFTER_MS = 90 * 1000;       // 画面を暗くするまで（cockpit と同じ）
constexpr uint32_t SLEEP_AFTER_MS = 3 * 60 * 1000; // スリープに入るまで
constexpr uint8_t DIM_BRIGHTNESS = 20;
uint32_t lastActivityMs = 0;
bool dimmed = false;
bool sleeping = false;
uint8_t normalBrightness = 0;
bool displayDirty = true; // 起きたときなど、内容が同じでも描き直したいときに立てる

uint16_t armToPulse(uint8_t arm)
{
    return map(arm, ShovelProtocol::ARM_MIN, ShovelProtocol::ARM_MAX, ARM_DOWN_US, ARM_UP_US);
}

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer* server) override { connected = true; }
    // 切断されたらアームはその場で止めておき、アドバタイズを再開して次の接続を待つ
    void onDisconnect(NimBLEServer* server) override { connected = false; }
};

class ArmCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic* characteristic) override
    {
        NimBLEAttValue value = characteristic->getValue();
        if (value.length() < 1)
        {
            return;
        }
        uint8_t arm = value.data()[0];
        targetArm = arm > ShovelProtocol::ARM_MAX ? ShovelProtocol::ARM_MAX : arm;
        lastActivityMs = millis();
    }
};

void setupBle()
{
    NimBLEDevice::init(ShovelProtocol::DEVICE_NAME);
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService* service = server->createService(ShovelProtocol::SERVICE_UUID);
    armCharacteristic = service->createCharacteristic(
        ShovelProtocol::ARM_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    armCharacteristic->setCallbacks(new ArmCallbacks());
    armCharacteristic->setValue(static_cast<uint8_t>(targetArm));
    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(ShovelProtocol::SERVICE_UUID);
    advertising->setScanResponse(true); // 名前をスキャン応答に載せる
    advertising->start();
}

// 目標のパルス幅になめらかに近づける。経過時間で動かす量を決めるので、ループの速さに左右されない
void updateServo()
{
    uint32_t nowUs = micros();
    float dtMs = (nowUs - lastUpdateUs) / 1000.0f;
    lastUpdateUs = nowUs;

    float diff = armToPulse(targetArm) - currentPulseUs;
    float step = diff * min(1.0f, dtMs / SERVO_SMOOTH_MS);
    float maxStep = SERVO_MAX_SPEED_US_PER_MS * dtMs;
    currentPulseUs += constrain(step, -maxStep, maxStep);

    uint16_t pulse = static_cast<uint16_t>(currentPulseUs + 0.5f);
    if (pulse != writtenPulseUs)
    {
        servo.writeMicroseconds(pulse);
        writtenPulseUs = pulse;
    }
}

// 1～5。電圧を読めていなければ 0
int batteryLevel()
{
    if (batteryMv <= 0)
    {
        return 0;
    }
    int level = 1;
    for (int threshold : BATTERY_LEVEL_MV)
    {
        if (batteryMv >= threshold)
        {
            level++;
        }
    }
    return level;
}

void updateBattery()
{
    uint32_t now = millis();
    if (lastBatteryReadMs != 0 && now - lastBatteryReadMs < BATTERY_READ_INTERVAL_MS)
    {
        return;
    }
    lastBatteryReadMs = now;

    float mv = analogReadMilliVolts(BATTERY_PIN) * 2.0f; // 半分にされているので2倍に戻す
    batteryMv = batteryMv <= 0 ? mv : batteryMv + (mv - batteryMv) * BATTERY_SMOOTHING;

    if (lastChargeCheckMs == 0)
    {
        lastChargeCheckMs = now;
        lastChargeCheckMv = batteryMv;
        return;
    }
    if (now - lastChargeCheckMs < CHARGE_CHECK_INTERVAL_MS)
    {
        return;
    }
    float diff = batteryMv - lastChargeCheckMv;
    if (diff >= CHARGE_RISE_MV || batteryMv >= CHARGE_FULL_MV)
    {
        charging = true;
    }
    else if (diff <= -CHARGE_FALL_MV)
    {
        charging = false;
    }
    lastChargeCheckMs = now;
    lastChargeCheckMv = batteryMv;
}

// 縦長の電池アイコン。下から5段階で塗る。充電中は水色にして稲妻を重ねる
void drawBattery(int x, int y, int level, bool isCharging, int mv)
{
    constexpr int W = 20;
    constexpr int H = 50;
    constexpr int SEG_GAP = 2;
    constexpr int SEG_H = (H - 4 - SEG_GAP * 4) / 5;
    auto& d = M5.Display;

    d.fillRect(x + W / 2 - 4, y - 3, 8, 3, TFT_DARKGREY);
    d.drawRoundRect(x, y, W, H, 3, TFT_DARKGREY);
    uint16_t color = isCharging ? TFT_CYAN : level >= 3 ? TFT_GREEN : level == 2 ? TFT_YELLOW : TFT_RED;
    for (int i = 0; i < level; i++)
    {
        int segY = y + H - 2 - (i + 1) * SEG_H - i * SEG_GAP;
        d.fillRect(x + 2, segY, W - 4, SEG_H, color);
    }
    if (isCharging)
    {
        int cx = x + W / 2;
        int cy = y + H / 2;
        d.fillTriangle(cx + 2, cy - 12, cx - 5, cy + 2, cx, cy + 2, TFT_WHITE);
        d.fillTriangle(cx - 2, cy + 12, cx + 5, cy - 2, cx, cy - 2, TFT_WHITE);
    }

    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.setTextDatum(top_center);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d.%dV", mv / 1000, (mv % 1000) / 100);
    d.drawString(mv > 0 ? buf : "--", x + W / 2, y + H + 6);
}

// 接続状態・アームの位置・電池の表示が変わったときだけ描き直す
void updateDisplay()
{
    static int lastArm = -1;
    static int lastConnected = -1;
    static int lastLevel = -1;
    static int lastCharging = -1;
    static int lastMv100 = -1;
    int arm = targetArm;
    int isConnected = connected ? 1 : 0;
    int level = batteryLevel();
    int isCharging = charging ? 1 : 0;
    int mv = static_cast<int>(batteryMv);
    int mv100 = mv / 100; // 0.1V 単位で変わったときだけ描き直す
    if (!displayDirty && arm == lastArm && isConnected == lastConnected && level == lastLevel &&
        isCharging == lastCharging && mv100 == lastMv100)
    {
        return;
    }
    displayDirty = false;
    lastArm = arm;
    lastConnected = isConnected;
    lastLevel = level;
    lastCharging = isCharging;
    lastMv100 = mv100;

    auto& d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextDatum(top_center);
    d.setTextColor(TFT_WHITE);
    d.setTextSize(2);
    d.drawString("SHOVEL", d.width() / 2, 4);

    d.setTextSize(1);
    d.setTextColor(isConnected ? TFT_GREEN : TFT_YELLOW);
    d.drawString(isConnected ? "connected" : "waiting cockpit", d.width() / 2, 26);

    // アームの位置を縦のバーで出す
    constexpr int BAR_W = 24;
    constexpr int BAR_TOP = 42;
    constexpr int BAR_H = 64;
    int x = (d.width() - BAR_W) / 2;
    d.drawRect(x, BAR_TOP, BAR_W, BAR_H, TFT_DARKGREY);
    int fillH = (BAR_H - 2) * arm / ShovelProtocol::ARM_MAX;
    d.fillRect(x + 1, BAR_TOP + BAR_H - 1 - fillH, BAR_W - 2, fillH, TFT_ORANGE);

    d.setTextColor(TFT_WHITE);
    d.drawString(String("ARM ") + arm, d.width() / 2, BAR_TOP + BAR_H + 6);

    // アームのバーの右側に電池
    drawBattery(96, BAR_TOP + 4, level, isCharging, mv);
}

// 操作があったら呼ぶ。暗くしていた画面を元に戻す
void markActivity()
{
    lastActivityMs = millis();
    if (dimmed)
    {
        M5.Display.setBrightness(normalBrightness);
        dimmed = false;
    }
}

// 放置時の電池消費を抑えるため、画面を消し、見つけてもらうための電波とサーボへの信号を止める
void enterSleep()
{
    NimBLEDevice::getAdvertising()->stop();
    servo.detach();
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    sleeping = true;
}

void wakeUp()
{
    M5.Display.wakeup();
    M5.Display.setBrightness(normalBrightness);
    dimmed = false;
    servo.attach(SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
    servo.writeMicroseconds(writtenPulseUs);
    lastUpdateUs = micros();
    NimBLEDevice::getAdvertising()->start();
    displayDirty = true;
    sleeping = false;
    markActivity();
}

// つながっていない間に触らない時間に応じて、画面を暗くし、スリープに入る
void checkIdle()
{
    if (connected)
    {
        markActivity(); // つながっている間は操作中とみなし、切れたところから数え始める
        return;
    }
    uint32_t idle = millis() - lastActivityMs;
    if (!dimmed && idle >= DIM_AFTER_MS)
    {
        M5.Display.setBrightness(DIM_BRIGHTNESS);
        dimmed = true;
    }
    if (idle >= SLEEP_AFTER_MS)
    {
        enterSleep();
    }
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    pinMode(BATTERY_PIN, INPUT);
    servo.attach(SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
    servo.writeMicroseconds(writtenPulseUs);
    lastUpdateUs = micros();

    setupBle();
    normalBrightness = M5.Display.getBrightness();
    lastActivityMs = millis();
}

void loop()
{
    M5.update();

    // スリープ中はボタンだけ見る。起こすために押したときはアームを動かさない
    if (sleeping)
    {
        if (M5.BtnA.wasPressed())
        {
            wakeUp();
        }
        delay(50);
        return;
    }

    // cockpit なしでも試せるよう、画面のボタンで上げ下げを切り替える
    if (M5.BtnA.wasPressed())
    {
        markActivity();
        targetArm = targetArm > ShovelProtocol::ARM_MIN ? ShovelProtocol::ARM_MIN : ShovelProtocol::ARM_MAX;
        armCharacteristic->setValue(static_cast<uint8_t>(targetArm));
    }

    updateServo();
    updateBattery();
    updateDisplay();
    checkIdle();
    delay(1);
}
