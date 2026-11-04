#include <M5Unified.h>

// 圧力センサーのテスト。BLE は使わず、読んだ値を Serial に出す
// センサーの出力は ATOM Lite の Grove 端子の白い線 (G32) につなぐ。G32 は無線と一緒でもアナログ入力が読める
// センサーの電源は Grove の黄色い線 (G26) を HIGH にして出す（約3.3V）。センサーに流れるのは 0.3mA 以下なので GPIO で足りる
// 注意: G32 に入れてよいのは3V前後まで。Grove の赤い線 (5V) にセンサーをじかにつながないこと
constexpr uint8_t SENSOR_PIN = 32;
constexpr uint8_t SENSOR_POWER_PIN = 26;
constexpr uint32_t PRINT_INTERVAL_MS = 50;

// 本体の LED（G27）を押した強さで光らせる。電池で動かして Serial を見られないときの確認用
constexpr uint8_t LED_PIN = 27;
constexpr int LED_IDLE_MAX_MV = 300;  // これ未満は押していない扱い。暗い青で電源が入っていることだけ示す
constexpr int LED_FULL_MV = 2900;     // これ以上は頭打ち。赤にする
constexpr uint8_t LED_BRIGHTNESS = 40; // 0〜255。まぶしくないよう控えめにする
constexpr uint8_t LED_IDLE_BLUE = 8;
constexpr int SAMPLES = 8; // 1回の表示ごとに平均をとる回数。値のちらつきを抑える

int minMv = INT32_MAX;
int maxMv = 0;
uint32_t lastPrintMs = 0;

int readSensorMv(int* raw)
{
    uint32_t sumMv = 0;
    uint32_t sumRaw = 0;
    for (int i = 0; i < SAMPLES; i++)
    {
        sumMv += analogReadMilliVolts(SENSOR_PIN);
        sumRaw += analogRead(SENSOR_PIN);
    }
    *raw = sumRaw / SAMPLES;
    return sumMv / SAMPLES;
}

// 押していなければ暗い青、押した強さに合わせて緑 → 黄 → 赤
void showPressureOnLed(int mv)
{
    if (mv < LED_IDLE_MAX_MV)
    {
        neopixelWrite(LED_PIN, 0, 0, LED_IDLE_BLUE);
        return;
    }
    int level = constrain(map(mv, LED_IDLE_MAX_MV, LED_FULL_MV, 0, 510), 0, 510);
    uint8_t red = min(level, 255) * LED_BRIGHTNESS / 255;
    uint8_t green = min(510 - level, 255) * LED_BRIGHTNESS / 255;
    neopixelWrite(LED_PIN, red, green, 0);
}

void resetMinMax()
{
    minMv = INT32_MAX;
    maxMv = 0;
    Serial.println("# min/max reset");
}

void setup()
{
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    pinMode(SENSOR_PIN, INPUT);
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db); // 約3.1Vまで読めるようにする

    Serial.println("# pedal sensor test");
    Serial.println("# raw,mV,min_mV,max_mV  (press button to reset min/max)");
}

void loop()
{
    M5.update();
    if (M5.BtnA.wasPressed())
    {
        resetMinMax();
    }

    uint32_t now = millis();
    if (now - lastPrintMs < PRINT_INTERVAL_MS)
    {
        return;
    }
    lastPrintMs = now;

    int raw;
    int mv = readSensorMv(&raw);
    minMv = min(minMv, mv);
    maxMv = max(maxMv, mv);
    Serial.printf("%d,%d,%d,%d\n", raw, mv, minMv, maxMv);
    showPressureOnLed(mv);
}
