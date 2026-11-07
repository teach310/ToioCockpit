#ifndef JOYSTICK_HPP
#define JOYSTICK_HPP

#include <Arduino.h>
#include <Wire.h>
#include "m5_unit_joystick2.hpp"

class Joystick
{
public:
    struct AxisCalibration
    {
        uint16_t min;
        uint16_t center;
        uint16_t max;

        AxisCalibration(uint16_t min = 0, uint16_t center = 32768, uint16_t max = 65535)
            : min(min), center(center), max(max) {}
    };

    struct Config
    {
        TwoWire* wire = &Wire;
        uint8_t i2cAddress = JOYSTICK2_ADDR;
        uint8_t sda = 21;
        uint8_t scl = 22;
        AxisCalibration xCalibration;
        AxisCalibration yCalibration;
        uint16_t deadzone = 1000;
    };

    bool begin(const Config& config);
    /// I2C を初期化し直して、もう一度 JoyStick を探す。begin() で見つからなかったときに呼び直す
    bool reconnect();
    /// 値を読み直す。I2C の読み取りに失敗したら、操作していない扱い（中央・押していない）にして false を返す
    /// 失敗が続いたら I2C を初期化し直して立て直す
    bool update();
    /// 今のX軸・Y軸の値を中心にする。スティックに触れていない起動直後に呼ぶ
    /// @param samples 平均をとる回数
    void calibrateCenter(int samples = 16);

    /// @return X軸の正規化値 (-100～100)
    int x() const { return _x; }
    /// @return Y軸の正規化値 (-100～100)
    int y() const { return _y; }
    /// @return X軸の生ADC値
    uint16_t rawX() const { return _rawX; }
    /// @return Y軸の生ADC値
    uint16_t rawY() const { return _rawY; }
    bool isPressed() const { return _currentPressed; }
    bool wasPressed() const { return !_lastPressed && _currentPressed; }
    bool wasReleased() const { return _lastPressed && !_currentPressed; }

    /// 直近の読み取りが成功しているか
    bool healthy() const { return _consecutiveFailures == 0; }
    /// 起動してからの読み取りの失敗回数（原因を調べる用）
    uint32_t failureCount() const { return _failureCount; }
    /// I2C を初期化し直した回数
    uint32_t recoveryCount() const { return _recoveryCount; }

    bool setRgbColor(uint32_t color);

private:
    // ライブラリの読み取りは失敗を知らせず、読めなかったバイトを 0xFF にしてしまう
    // （スティックを一番倒した・ボタンを押していない扱いになる）ので、失敗がわかるように自前で読む
    bool readRegister(uint8_t reg, uint8_t* buffer, uint8_t length);
    int normalize(uint16_t value, const AxisCalibration& cal) const;

    M5UnitJoystick2 _unit;
    Config _config;
    int _x = 0;
    int _y = 0;
    uint16_t _rawX = 0;
    uint16_t _rawY = 0;
    bool _lastPressed = false;
    bool _currentPressed = false;
    uint32_t _consecutiveFailures = 0;
    uint32_t _failureCount = 0;
    uint32_t _recoveryCount = 0;
};

#endif
