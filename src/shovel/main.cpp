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

Servo servo;
float currentPulseUs = ARM_DOWN_US;
uint16_t writtenPulseUs = ARM_DOWN_US;
uint32_t lastUpdateUs = 0;

// BLE のコールバックは別のタスクから呼ばれるので volatile にする
volatile uint8_t targetArm = ShovelProtocol::ARM_MIN;
volatile bool connected = false;

NimBLECharacteristic* armCharacteristic = nullptr;

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

// 接続状態とアームの位置が変わったときだけ描き直す
void updateDisplay()
{
    static int lastArm = -1;
    static int lastConnected = -1;
    int arm = targetArm;
    int isConnected = connected ? 1 : 0;
    if (arm == lastArm && isConnected == lastConnected)
    {
        return;
    }
    lastArm = arm;
    lastConnected = isConnected;

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
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    servo.attach(SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
    servo.writeMicroseconds(writtenPulseUs);
    lastUpdateUs = micros();

    setupBle();
}

void loop()
{
    M5.update();

    // cockpit なしでも試せるよう、画面のボタンで上げ下げを切り替える
    if (M5.BtnA.wasPressed())
    {
        targetArm = targetArm > ShovelProtocol::ARM_MIN ? ShovelProtocol::ARM_MIN : ShovelProtocol::ARM_MAX;
        armCharacteristic->setValue(static_cast<uint8_t>(targetArm));
    }

    updateServo();
    updateDisplay();
    delay(1);
}
