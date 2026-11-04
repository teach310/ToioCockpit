#include "Dashboard.hpp"

namespace
{
    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    // 色
    constexpr uint16_t COLOR_BG = rgb(6, 8, 14);
    constexpr uint16_t COLOR_PANEL = rgb(18, 22, 32);
    constexpr uint16_t COLOR_TRACK = rgb(36, 40, 54);
    constexpr uint16_t COLOR_DIM = rgb(80, 88, 104);
    constexpr uint16_t COLOR_TEXT = rgb(220, 228, 240);
    constexpr uint16_t COLOR_CYAN = rgb(0, 200, 255);
    constexpr uint16_t COLOR_YELLOW = rgb(255, 210, 0);
    constexpr uint16_t COLOR_RED = rgb(255, 60, 60);
    constexpr uint16_t COLOR_GREEN = rgb(60, 220, 110);
    constexpr uint16_t COLOR_ORANGE = rgb(255, 140, 0);
    // 押せるボタン。少し明るい面に上の縁の光と下の影を付けて、浮いて見えるようにする
    constexpr uint16_t COLOR_BUTTON = rgb(34, 40, 56);
    constexpr uint16_t COLOR_BUTTON_EDGE = rgb(70, 78, 98);
    constexpr uint16_t COLOR_BUTTON_LIGHT = rgb(96, 106, 130);
    constexpr uint16_t COLOR_BUTTON_SHADOW = rgb(0, 0, 0);

    // レイアウト (320x240)
    constexpr int STATUS_BAR_HEIGHT = 26;
    constexpr int GAUGE_X = 160;
    constexpr int GAUGE_Y = 132;
    constexpr int GAUGE_R_OUT = 95;
    constexpr int GAUGE_R_IN = 83;
    constexpr int HORN_R = 40;
    // メーターは左下(135度)から右下(405度)までの270度。角度は3時の方向が0で時計回り
    constexpr int GAUGE_START_DEG = 135;
    constexpr int GAUGE_SWEEP_DEG = 270;
    constexpr int GAUGE_STEP_DEG = 3; // 色を少しずつ変えるため、この角度ごとに分けて塗る

    // ボタン
    constexpr int BUTTON_RADIUS = 6;
    constexpr int BUTTON_DEPTH = 3; // 影の厚み。押すとこの分だけ沈む
    // 左上: 速さのギア
    constexpr int GEAR_BTN_X = 4;
    constexpr int GEAR_BTN_Y = 32;
    constexpr int GEAR_BTN_W = 44;
    constexpr int GEAR_BTN_H = 40;
    // 左下: ショベル、右下: ハンドルの切り方。真ん中の速さの数字とは重ねない
    constexpr int BOTTOM_BTN_Y = 202;
    constexpr int BOTTOM_BTN_W = 100;
    constexpr int BOTTOM_BTN_H = 34;
    constexpr int SHOVEL_BTN_X = 4;
    constexpr int STEER_BTN_X = 216;
    // タッチの判定はボタンより少し広めに取って、指で押しやすくする
    constexpr int HIT_MARGIN = 6;
    // ギアの列 (P R N D S)。速さのギアのボタンの下に並べる
    constexpr int GEAR_LIST_TOP = 94;
    constexpr int GEAR_LIST_STEP = 22;

    constexpr uint32_t FRAME_INTERVAL_MS = 66; // 画面全体の転送に時間がかかるので約15fpsに抑える

    uint16_t lerpColor(uint16_t a, uint16_t b, float t)
    {
        auto ch = [t](int x, int y) { return static_cast<int>(x + (y - x) * t); };
        int r = ch((a >> 11) & 0x1F, (b >> 11) & 0x1F);
        int g = ch((a >> 5) & 0x3F, (b >> 5) & 0x3F);
        int bl = ch(a & 0x1F, b & 0x1F);
        return (r << 11) | (g << 5) | bl;
    }

    // 速さの割合で 水色 → 黄 → 赤 と変える
    uint16_t speedColor(float ratio)
    {
        if (ratio < 0.7f)
        {
            return lerpColor(COLOR_CYAN, COLOR_YELLOW, ratio / 0.7f);
        }
        return lerpColor(COLOR_YELLOW, COLOR_RED, (ratio - 0.7f) / 0.3f);
    }

    uint16_t batteryColor(int percent)
    {
        if (percent > 50)
        {
            return COLOR_GREEN;
        }
        return percent > 20 ? COLOR_YELLOW : COLOR_RED;
    }

    float degToRad(float deg) { return deg * PI / 180.0f; }
}

void Dashboard::begin()
{
    _canvas.setColorDepth(16);
    _canvas.setPsram(true);
    _canvas.createSprite(_display.width(), _display.height());
    _dirty = true;
}

void Dashboard::setStatus(const char* status, const char* detail, Tone tone)
{
    if (strcmp(status, _status) == 0 && strcmp(detail, _detail) == 0 && tone == _tone)
    {
        return;
    }
    _status = status;
    _detail = detail;
    _tone = tone;
    _dirty = true;
}

void Dashboard::setRunning(bool running)
{
    if (running != _running)
    {
        _running = running;
        _dirty = true;
    }
}

void Dashboard::setSpeed(int speed)
{
    if (speed != _speed)
    {
        _speed = speed;
        _dirty = true;
    }
}

void Dashboard::setGear(Gear gear)
{
    if (gear != _gear)
    {
        _gear = gear;
        _dirty = true;
    }
}

void Dashboard::setCoreBattery(int percent, bool charging)
{
    if (percent != _coreBattery || charging != _coreCharging)
    {
        _coreBattery = percent;
        _coreCharging = charging;
        _dirty = true;
    }
}

void Dashboard::setToioBattery(int percent)
{
    if (percent != _toioBattery)
    {
        _toioBattery = percent;
        _dirty = true;
    }
}

void Dashboard::setHornPressed(bool pressed)
{
    if (pressed != _hornPressed)
    {
        _hornPressed = pressed;
        _dirty = true;
    }
}

void Dashboard::setShovel(ShovelState state, int arm)
{
    if (state != ShovelState::CONNECTED)
    {
        arm = -1;
    }
    if (state != _shovelState || arm != _shovelArm)
    {
        _shovelState = state;
        _shovelArm = arm;
        _dirty = true;
    }
}

void Dashboard::setStickSteering(bool stick)
{
    if (stick != _stickSteering)
    {
        _stickSteering = stick;
        _dirty = true;
    }
}

void Dashboard::setSpeedGear(SpeedGear gear)
{
    if (gear != _speedGear)
    {
        _speedGear = gear;
        _dirty = true;
    }
}

void Dashboard::setTouch(bool pressed, int x, int y)
{
    TouchedButton touched = TouchedButton::NONE;
    if (pressed)
    {
        if (isGearHit(x, y))
        {
            touched = TouchedButton::GEAR;
        }
        else if (isShovelHit(x, y))
        {
            touched = TouchedButton::SHOVEL;
        }
        else if (isSteeringHit(x, y))
        {
            touched = TouchedButton::STEER;
        }
    }
    if (touched != _touched)
    {
        _touched = touched;
        _dirty = true;
    }
}

namespace
{
    bool hitButton(int x, int y, int bx, int by, int bw, int bh)
    {
        return x >= bx - HIT_MARGIN && x < bx + bw + HIT_MARGIN &&
               y >= by - HIT_MARGIN && y < by + bh + BUTTON_DEPTH + HIT_MARGIN;
    }
}

bool Dashboard::isGearHit(int x, int y) const
{
    return hitButton(x, y, GEAR_BTN_X, GEAR_BTN_Y, GEAR_BTN_W, GEAR_BTN_H);
}

bool Dashboard::isSteeringHit(int x, int y) const
{
    return hitButton(x, y, STEER_BTN_X, BOTTOM_BTN_Y, BOTTOM_BTN_W, BOTTOM_BTN_H);
}

bool Dashboard::isShovelHit(int x, int y) const
{
    return hitButton(x, y, SHOVEL_BTN_X, BOTTOM_BTN_Y, BOTTOM_BTN_W, BOTTOM_BTN_H);
}

bool Dashboard::isHornHit(int x, int y) const
{
    int dx = x - GAUGE_X;
    int dy = y - GAUGE_Y;
    return dx * dx + dy * dy <= HORN_R * HORN_R;
}

void Dashboard::draw(bool force)
{
    uint32_t now = millis();
    if (!force && (!_dirty || now - _lastDrawMs < FRAME_INTERVAL_MS))
    {
        return;
    }
    _lastDrawMs = now;

    // 針は実際の値に少しずつ近づける。近づききるまでは次のフレームも描く
    float target = static_cast<float>(abs(_speed));
    float diff = target - _shownSpeed;
    if (fabsf(diff) < 0.5f)
    {
        _shownSpeed = target;
        _dirty = false;
    }
    else
    {
        _shownSpeed += diff * 0.45f;
    }

    _canvas.fillScreen(COLOR_BG);
    drawStatusBar();
    drawGauge();
    drawCenter();
    drawSpeedText();
    drawGears();
    drawSpeedGear();
    drawBatteries();
    drawShovel();
    drawSteering();
    _canvas.pushSprite(0, 0);
}

void Dashboard::drawStatusBar()
{
    uint16_t toneColor = COLOR_CYAN;
    switch (_tone)
    {
    case Tone::OK:
        toneColor = COLOR_GREEN;
        break;
    case Tone::WARN:
        toneColor = COLOR_YELLOW;
        break;
    case Tone::ERROR:
        toneColor = COLOR_RED;
        break;
    case Tone::INFO:
        break;
    }

    _canvas.fillRect(0, 0, _canvas.width(), STATUS_BAR_HEIGHT, COLOR_PANEL);
    _canvas.drawFastHLine(0, STATUS_BAR_HEIGHT, _canvas.width(), toneColor);
    _canvas.fillCircle(12, STATUS_BAR_HEIGHT / 2, 5, toneColor);

    _canvas.setFont(&fonts::FreeSansBold9pt7b);
    _canvas.setTextDatum(middle_left);
    _canvas.setTextColor(COLOR_TEXT);
    _canvas.drawString(_status, 24, STATUS_BAR_HEIGHT / 2);

    _canvas.setFont(&fonts::Font0);
    _canvas.setTextDatum(middle_right);
    _canvas.setTextColor(COLOR_DIM);
    _canvas.drawString("toio COCKPIT", _canvas.width() - 8, STATUS_BAR_HEIGHT / 2);
}

void Dashboard::drawGauge()
{
    // 下地のリング
    for (int a = 0; a < GAUGE_SWEEP_DEG; a += GAUGE_STEP_DEG)
    {
        int a0 = (GAUGE_START_DEG + a) % 360;
        _canvas.fillArc(GAUGE_X, GAUGE_Y, GAUGE_R_OUT, GAUGE_R_IN, a0, a0 + GAUGE_STEP_DEG, COLOR_TRACK);
    }

    // 速さの分だけ色を付ける。後退中はオレンジ一色
    float ratio = _shownSpeed / 100.0f;
    int filled = static_cast<int>(GAUGE_SWEEP_DEG * ratio);
    bool reverse = _speed < 0;
    for (int a = 0; a < filled; a += GAUGE_STEP_DEG)
    {
        int a0 = (GAUGE_START_DEG + a) % 360;
        int width = min(GAUGE_STEP_DEG, filled - a);
        uint16_t color = reverse ? COLOR_ORANGE : speedColor(static_cast<float>(a) / GAUGE_SWEEP_DEG);
        _canvas.fillArc(GAUGE_X, GAUGE_Y, GAUGE_R_OUT, GAUGE_R_IN, a0, a0 + width, color);
    }

    // 目盛り。20ごとに長い線と数字
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextDatum(middle_center);
    for (int v = 0; v <= 100; v += 5)
    {
        float rad = degToRad(GAUGE_START_DEG + GAUGE_SWEEP_DEG * v / 100.0f);
        bool major = v % 20 == 0;
        int r0 = GAUGE_R_IN - 4;
        int r1 = r0 - (major ? 9 : 4);
        _canvas.drawLine(GAUGE_X + cosf(rad) * r0, GAUGE_Y + sinf(rad) * r0,
                         GAUGE_X + cosf(rad) * r1, GAUGE_Y + sinf(rad) * r1,
                         major ? COLOR_TEXT : COLOR_DIM);
        if (major)
        {
            int rt = r1 - 9;
            _canvas.setTextColor(COLOR_DIM);
            _canvas.drawNumber(v, GAUGE_X + cosf(rad) * rt, GAUGE_Y + sinf(rad) * rt);
        }
    }

    // 針の先の光る点
    if (_running)
    {
        float rad = degToRad(GAUGE_START_DEG + GAUGE_SWEEP_DEG * ratio);
        int r = (GAUGE_R_OUT + GAUGE_R_IN) / 2;
        _canvas.fillCircle(GAUGE_X + cosf(rad) * r, GAUGE_Y + sinf(rad) * r, 6, COLOR_TEXT);
    }
}

void Dashboard::drawCenter()
{
    if (!_running)
    {
        // 走れないときはクラクションの代わりに、いまの状況を出す
        _canvas.fillCircle(GAUGE_X, GAUGE_Y, HORN_R + 14, COLOR_PANEL);
        _canvas.setFont(&fonts::Font0);
        _canvas.setTextDatum(middle_center);
        _canvas.setTextColor(COLOR_TEXT);
        _canvas.drawString(_detail, GAUGE_X, GAUGE_Y);
        return;
    }

    // ほかのボタンと同じく、押していないときは影で浮かせ、押すと沈んでオレンジに光る
    int cy = GAUGE_Y;
    if (_hornPressed)
    {
        cy += BUTTON_DEPTH;
        _canvas.fillCircle(GAUGE_X, cy, HORN_R + 5, lerpColor(COLOR_BG, COLOR_ORANGE, 0.4f));
        _canvas.fillCircle(GAUGE_X, cy, HORN_R, COLOR_ORANGE);
    }
    else
    {
        _canvas.fillCircle(GAUGE_X, GAUGE_Y + BUTTON_DEPTH, HORN_R, COLOR_BUTTON_SHADOW);
        _canvas.fillCircle(GAUGE_X, GAUGE_Y, HORN_R, COLOR_BUTTON);
        _canvas.drawCircle(GAUGE_X, GAUGE_Y, HORN_R, COLOR_BUTTON_EDGE);
        // 左上の縁を明るくして、光が当たっているように見せる
        _canvas.drawArc(GAUGE_X, GAUGE_Y, HORN_R - 2, HORN_R - 3, 190, 290, COLOR_BUTTON_LIGHT);
    }
    _canvas.setFont(&fonts::FreeSansBold9pt7b);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(_hornPressed ? COLOR_BG : COLOR_TEXT);
    _canvas.drawString("HORN", GAUGE_X, cy);
}

void Dashboard::drawSpeedText()
{
    // デジタル表示はメーターの下のすき間に置く
    int speed = static_cast<int>(_shownSpeed + 0.5f);
    _canvas.setFont(&fonts::Font7);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(speed == 0 ? COLOR_DIM : COLOR_TEXT);
    _canvas.drawNumber(speed, GAUGE_X, 200);

    _canvas.setFont(&fonts::Font0);
    _canvas.setTextColor(COLOR_DIM);
    _canvas.drawString("km/h", GAUGE_X, 231);
}

void Dashboard::drawGears()
{
    static const struct
    {
        Gear gear;
        const char* label;
    } GEARS[] = {
        {Gear::P, "P"}, {Gear::R, "R"}, {Gear::N, "N"}, {Gear::D, "D"}, {Gear::S, "S"},
    };

    // 今のギアを表すだけで押せないので、ボタンの形にはしない
    _canvas.setFont(&fonts::FreeSansBold9pt7b);
    _canvas.setTextDatum(middle_center);
    int y = GEAR_LIST_TOP;
    for (const auto& g : GEARS)
    {
        if (g.gear == _gear)
        {
            uint16_t color = g.gear == Gear::R ? COLOR_ORANGE : COLOR_CYAN;
            _canvas.fillRoundRect(12, y - 10, 24, 20, 5, color);
            _canvas.setTextColor(COLOR_BG);
        }
        else
        {
            _canvas.setTextColor(COLOR_DIM);
        }
        _canvas.drawString(g.label, 24, y);
        y += GEAR_LIST_STEP;
    }
}

int Dashboard::drawButton(int x, int y, int w, int h, bool pressed, uint16_t accent)
{
    if (pressed)
    {
        // 沈んだ位置に、accent の色を薄く混ぜた面と縁
        int py = y + BUTTON_DEPTH;
        _canvas.fillRoundRect(x, py, w, h, BUTTON_RADIUS, lerpColor(COLOR_BUTTON, accent, 0.35f));
        _canvas.drawRoundRect(x, py, w, h, BUTTON_RADIUS, accent);
        return BUTTON_DEPTH;
    }
    _canvas.fillRoundRect(x, y + BUTTON_DEPTH, w, h, BUTTON_RADIUS, COLOR_BUTTON_SHADOW);
    _canvas.fillRoundRect(x, y, w, h, BUTTON_RADIUS, COLOR_BUTTON);
    _canvas.drawRoundRect(x, y, w, h, BUTTON_RADIUS, COLOR_BUTTON_EDGE);
    _canvas.drawFastHLine(x + BUTTON_RADIUS, y + 1, w - BUTTON_RADIUS * 2, COLOR_BUTTON_LIGHT);
    return 0;
}

// 左上の速さのギアのボタン。名前と、遅い順に3つ並べた目盛りで今の段を出す。タッチで次の段へ
void Dashboard::drawSpeedGear()
{
    static const struct
    {
        SpeedGear gear;
        const char* label;
        uint16_t color;
    } SPEED_GEARS[] = {
        {SpeedGear::SLOW, "SLOW", COLOR_GREEN},
        {SpeedGear::LO, "LOW", COLOR_CYAN},
        {SpeedGear::HI, "HIGH", COLOR_ORANGE}, // 一番速いのは目立つようオレンジ
    };
    constexpr int LEVELS = sizeof(SPEED_GEARS) / sizeof(SPEED_GEARS[0]);
    int level = static_cast<int>(_speedGear);
    const auto& current = SPEED_GEARS[level];

    int dy = drawButton(GEAR_BTN_X, GEAR_BTN_Y, GEAR_BTN_W, GEAR_BTN_H, _touched == TouchedButton::GEAR, current.color);
    int cx = GEAR_BTN_X + GEAR_BTN_W / 2;
    int top = GEAR_BTN_Y + dy;

    _canvas.setFont(&fonts::Font2);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(current.color);
    _canvas.drawString(current.label, cx, top + 14);

    constexpr int BAR_W = 8;
    constexpr int BAR_H = 5;
    constexpr int BAR_GAP = 3;
    int barX = cx - (BAR_W * LEVELS + BAR_GAP * (LEVELS - 1)) / 2;
    for (int i = 0; i < LEVELS; i++)
    {
        _canvas.fillRect(barX + i * (BAR_W + BAR_GAP), top + 28, BAR_W, BAR_H, i <= level ? current.color : COLOR_TRACK);
    }
}

void Dashboard::drawBatteries()
{
    drawBattery(262, "CORE", _coreBattery, _coreCharging);
    drawBattery(292, "TOIO", _toioBattery, false);
}

// 縦長の電池アイコン。下から残量の分だけ塗る
void Dashboard::drawBattery(int x, const char* label, int percent, bool charging)
{
    constexpr int W = 18;
    constexpr int TOP = 52;
    constexpr int H = 120;
    int cx = x + W / 2;

    _canvas.fillRect(cx - 4, TOP - 4, 8, 4, COLOR_DIM);
    _canvas.drawRoundRect(x, TOP, W, H, 3, COLOR_DIM);
    if (percent >= 0)
    {
        int fillH = (H - 4) * min(percent, 100) / 100;
        _canvas.fillRect(x + 2, TOP + H - 2 - fillH, W - 4, fillH, charging ? COLOR_CYAN : batteryColor(percent));
    }
    if (charging)
    {
        // 充電中の稲妻マーク
        int cy = TOP + H / 2;
        _canvas.fillTriangle(cx + 2, cy - 14, cx - 5, cy + 2, cx, cy + 2, COLOR_TEXT);
        _canvas.fillTriangle(cx - 2, cy + 14, cx + 5, cy - 2, cx, cy - 2, COLOR_TEXT);
    }

    _canvas.setFont(&fonts::Font0);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(COLOR_DIM);
    _canvas.drawString(label, cx, TOP + H + 12);
    _canvas.setTextColor(COLOR_TEXT);
    if (percent >= 0)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        _canvas.drawString(buf, cx, TOP + H + 24);
    }
    else
    {
        _canvas.drawString("--", cx, TOP + H + 24);
    }
}

// 左下のボタンに、ショベルのつながりとアームの位置を横長のバーで出す。探していないときは「TAP」でタッチを促す
void Dashboard::drawShovel()
{
    int dy = drawButton(SHOVEL_BTN_X, BOTTOM_BTN_Y, BOTTOM_BTN_W, BOTTOM_BTN_H, _touched == TouchedButton::SHOVEL, COLOR_CYAN);
    const int X = SHOVEL_BTN_X + 8;
    const int W = BOTTOM_BTN_W - 16;
    const int LABEL_Y = BOTTOM_BTN_Y + dy + 10;
    const int BAR_Y = BOTTOM_BTN_Y + dy + 19;
    constexpr int BAR_H = 9;

    _canvas.setFont(&fonts::Font0);
    _canvas.setTextDatum(middle_left);
    bool connected = _shovelState == ShovelState::CONNECTED;
    _canvas.setTextColor(connected ? COLOR_TEXT : COLOR_DIM);
    _canvas.drawString("SHOVEL", X, LABEL_Y);

    _canvas.drawRoundRect(X, BAR_Y, W, BAR_H, 2, COLOR_DIM);
    if (!connected || _shovelArm < 0)
    {
        _canvas.setTextDatum(middle_right);
        bool searching = _shovelState == ShovelState::SEARCHING;
        _canvas.setTextColor(searching ? COLOR_CYAN : COLOR_DIM);
        _canvas.drawString(searching ? "SEARCH" : "TAP", X + W, LABEL_Y);
        return;
    }
    int fillW = (W - 4) * min(_shovelArm, 100) / 100;
    _canvas.fillRect(X + 2, BAR_Y + 2, fillW, BAR_H - 4, COLOR_ORANGE);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", _shovelArm);
    _canvas.setTextDatum(middle_right);
    _canvas.drawString(buf, X + W, LABEL_Y);
}

// 右下のボタンに、走っているときのハンドルの切り方を出す。タッチで切り替わる
void Dashboard::drawSteering()
{
    int dy = drawButton(STEER_BTN_X, BOTTOM_BTN_Y, BOTTOM_BTN_W, BOTTOM_BTN_H, _touched == TouchedButton::STEER, COLOR_CYAN);
    int top = BOTTOM_BTN_Y + dy;

    _canvas.setFont(&fonts::Font0);
    _canvas.setTextDatum(middle_left);
    _canvas.setTextColor(COLOR_DIM);
    _canvas.drawString("STEER", STEER_BTN_X + 8, top + 10);

    _canvas.setFont(&fonts::Font2);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(COLOR_CYAN);
    _canvas.drawString(_stickSteering ? "STICK" : "TILT", STEER_BTN_X + BOTTOM_BTN_W / 2, top + 23);
}
