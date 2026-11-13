#include <M5Unified.h>

// 圧力センサーのテスト。BLE は使わず、読んだ値を Serial と画面に出す
// 感圧センサーと 10kΩ を CoreS3 の Port B（黒い Grove）につなぐ
// Port A は I2C 用で、基板の中で 3.3V に引き上げられているのでセンサーには使わない
//   黄 (G9): センサーの電源。HIGH にして約3.3Vを出す。センサーに流れるのは 0.3mA 以下なので GPIO で足りる
//   白 (G8): センサーと 10kΩ のあいだの電圧を読む。G8 は ADC1 なので無線と一緒でもアナログ入力が読める
//   黒 (GND): 10kΩ のもう一方
// 注意: G8 に入れてよいのは3V前後まで。Port B の赤い線 (5V) は使わず、先を絶縁すること
constexpr uint8_t SENSOR_POWER_PIN = 9;
constexpr uint8_t SENSOR_PIN = 8;
constexpr uint32_t UPDATE_INTERVAL_MS = 50;
constexpr int SAMPLES = 8; // 1回の表示ごとに平均をとる回数。値のちらつきを抑える

// 画面のメーター。今後アクセルの 0〜100 にするときの目安を兼ねる
constexpr int IDLE_MAX_MV = 300; // これ未満は押していない扱い
constexpr int FULL_MV = 2900;    // これ以上は頭打ち
constexpr uint8_t DISPLAY_BRIGHTNESS = 80;

M5Canvas canvas(&M5.Display);

int minMv = INT32_MAX;
int maxMv = 0;
uint32_t lastUpdateMs = 0;

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

int pressurePercent(int mv)
{
    if (mv < IDLE_MAX_MV)
    {
        return 0;
    }
    return constrain(map(mv, IDLE_MAX_MV, FULL_MV, 0, 100), 0, 100);
}

// 0% は緑、50% は黄、100% は赤
uint16_t pressureColor(int percent)
{
    int level = percent * 510 / 100;
    return M5.Display.color565(min(level, 255), min(510 - level, 255), 0);
}

void drawScreen(int raw, int mv)
{
    const int w = canvas.width();
    const int percent = pressurePercent(mv);
    const uint16_t color = percent > 0 ? pressureColor(percent) : TFT_DARKGREY;

    canvas.fillSprite(TFT_BLACK);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_left);
    canvas.drawString("PEDAL SENSOR TEST", 12, 10);

    // 押した強さ（%）を大きく出す
    canvas.setTextColor(color);
    canvas.setFont(&fonts::Font7);
    canvas.setTextDatum(middle_right);
    canvas.drawNumber(percent, w - 70, 80);
    canvas.setFont(&fonts::Font4);
    canvas.setTextDatum(middle_left);
    canvas.drawString("%", w - 62, 92);

    // バー
    const int barX = 12, barY = 132, barW = w - 24, barH = 28;
    canvas.drawRoundRect(barX, barY, barW, barH, 6, TFT_DARKGREY);
    if (percent > 0)
    {
        canvas.fillRoundRect(barX + 3, barY + 3, (barW - 6) * percent / 100, barH - 6, 4, color);
    }

    // 電圧と min/max
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString(String(mv) + " mV  (raw " + String(raw) + ")", 12, 172);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("min " + String(minMv) + " mV   max " + String(maxMv) + " mV", 12, 194);

    // 画面の下の左のボタン（BtnA）
    canvas.setTextColor(TFT_DARKGREY);
    canvas.setTextDatum(bottom_left);
    canvas.drawString("[A] reset min/max", 12, canvas.height() - 4);

    canvas.pushSprite(0, 0);
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
    M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
    canvas.createSprite(M5.Display.width(), M5.Display.height());

    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    pinMode(SENSOR_PIN, INPUT);
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db); // 約3.1Vまで読めるようにする

    Serial.println("# pedal sensor test");
    Serial.println("# raw,mV,min_mV,max_mV  (press BtnA to reset min/max)");
}

void loop()
{
    M5.update();
    if (M5.BtnA.wasPressed())
    {
        resetMinMax();
    }

    uint32_t now = millis();
    if (now - lastUpdateMs < UPDATE_INTERVAL_MS)
    {
        return;
    }
    lastUpdateMs = now;

    int raw;
    int mv = readSensorMv(&raw);
    minMv = min(minMv, mv);
    maxMv = max(maxMv, mv);
    Serial.printf("%d,%d,%d,%d\n", raw, mv, minMv, maxMv);
    drawScreen(raw, mv);
}
