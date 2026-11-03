#ifndef DASHBOARD_HPP
#define DASHBOARD_HPP

#include <M5Unified.h>

// 車のメーターパネル風の画面
// 値をセットして draw() を呼ぶと、変化があったときだけ画面全体を描き直す
class Dashboard
{
public:
    enum class Gear { P, R, N, D, S };
    enum class ShovelState { OFF, SEARCHING, CONNECTED };
    // ステータス表示の色
    enum class Tone { INFO, OK, WARN, ERROR };

    explicit Dashboard(M5GFX& display) : _display(display), _canvas(&display) {}

    void begin();

    void setStatus(const char* status, const char* detail, Tone tone);
    /// @param running true のときはメーターとクラクションボタン、false のときは中央に detail を出す
    void setRunning(bool running);
    /// @param speed スロットル (-100～100)
    void setSpeed(int speed);
    void setGear(Gear gear);
    void setCoreBattery(int percent, bool charging);
    /// @param percent 0未満は不明
    void setToioBattery(int percent);
    void setHornPressed(bool pressed);
    /// @param arm アームの位置 (0～100)。CONNECTED 以外では使わない
    void setShovel(ShovelState state, int arm);

    bool isHornHit(int x, int y) const;
    /// 左下のショベル表示に触れたか。タッチで探し直すのに使う
    bool isShovelHit(int x, int y) const;

    /// @param force true のときは描き直しの間隔を待たずにすぐ描く
    void draw(bool force = false);

private:
    void drawStatusBar();
    void drawGauge();
    void drawCenter();
    void drawSpeedText();
    void drawGears();
    void drawBatteries();
    void drawBattery(int x, const char* label, int percent, bool charging);
    void drawShovel();

    M5GFX& _display;
    LGFX_Sprite _canvas;

    const char* _status = "";
    const char* _detail = "";
    Tone _tone = Tone::INFO;
    bool _running = false;
    int _speed = 0;
    float _shownSpeed = 0; // メーターの針をなめらかに動かすための表示用の値
    Gear _gear = Gear::P;
    int _coreBattery = -1;
    bool _coreCharging = false;
    int _toioBattery = -1;
    bool _hornPressed = false;
    ShovelState _shovelState = ShovelState::OFF;
    int _shovelArm = -1;

    bool _dirty = true;
    uint32_t _lastDrawMs = 0;
};

#endif
