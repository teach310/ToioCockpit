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
    void update();

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

    void setRgbColor(uint32_t color) { _unit.set_rgb_color(color); }

private:
    int normalize(uint16_t value, const AxisCalibration& cal) const;

    M5UnitJoystick2 _unit;
    Config _config;
    int _x = 0;
    int _y = 0;
    uint16_t _rawX = 0;
    uint16_t _rawY = 0;
    bool _lastPressed = false;
    bool _currentPressed = false;
};

#endif
