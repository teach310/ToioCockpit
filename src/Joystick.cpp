#include "Joystick.hpp"

bool Joystick::begin(const Config& config)
{
    _config = config;
    return _unit.begin(
        _config.wire,
        _config.i2cAddress,
        _config.sda,
        _config.scl);
}

void Joystick::update()
{
    _unit.get_joy_adc_16bits_value_xy(&_rawX, &_rawY);
    _x = normalize(_rawX, _config.xCalibration);
    _y = normalize(_rawY, _config.yCalibration);

    _lastPressed = _currentPressed;
    _currentPressed = (_unit.get_button_value() == 0);
}

int Joystick::normalize(uint16_t value, const AxisCalibration& cal) const
{
    const int min = static_cast<int>(cal.min);
    const int center = static_cast<int>(cal.center);
    const int max = static_cast<int>(cal.max);
    const int delta = static_cast<int>(value) - center;

    if (abs(delta) <= static_cast<int>(_config.deadzone))
    {
        return 0;
    }

    if (delta > 0)
    {
        return (delta - _config.deadzone) * 100 / (max - center - _config.deadzone);
    }

    return -((-delta - _config.deadzone) * 100 / (center - min - _config.deadzone));
}
