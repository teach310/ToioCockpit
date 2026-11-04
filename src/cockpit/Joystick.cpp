#include "Joystick.hpp"

namespace
{
    // この回数続けて読み取りに失敗したら、I2C を初期化し直す（50ms ごとに読むなら約0.5秒）
    constexpr uint32_t RECOVER_AFTER_FAILURES = 10;
}

bool Joystick::begin(const Config& config)
{
    _config = config;
    return _unit.begin(
        _config.wire,
        _config.i2cAddress,
        _config.sda,
        _config.scl);
}

bool Joystick::readRegister(uint8_t reg, uint8_t* buffer, uint8_t length)
{
    TwoWire* wire = _config.wire;
    wire->beginTransmission(_config.i2cAddress);
    wire->write(reg);
    if (wire->endTransmission(false) != 0)
    {
        return false;
    }
    if (wire->requestFrom(_config.i2cAddress, length) != length)
    {
        return false;
    }
    for (uint8_t i = 0; i < length; i++)
    {
        buffer[i] = wire->read();
    }
    return true;
}

bool Joystick::update()
{
    uint8_t adc[4];
    uint8_t button;
    bool ok = readRegister(JOYSTICK2_ADC_VALUE_12BITS_REG, adc, sizeof(adc)) &&
              readRegister(JOYSTICK2_BUTTON_REG, &button, 1);

    _lastPressed = _currentPressed;
    if (!ok)
    {
        // 読めなかったときは、勝手に動き続けないよう操作していない扱いにする
        _x = 0;
        _y = 0;
        _currentPressed = false;
        _failureCount++;
        _consecutiveFailures++;
        if (_consecutiveFailures % RECOVER_AFTER_FAILURES == 0)
        {
            _config.wire->end();
            _unit.begin(_config.wire, _config.i2cAddress, _config.sda, _config.scl);
            _recoveryCount++;
        }
        return false;
    }

    _consecutiveFailures = 0;
    memcpy(&_rawX, &adc[0], 2);
    memcpy(&_rawY, &adc[2], 2);
    _x = normalize(_rawX, _config.xCalibration);
    _y = normalize(_rawY, _config.yCalibration);
    _currentPressed = (button == 0);
    return true;
}

bool Joystick::setRgbColor(uint32_t color)
{
    TwoWire* wire = _config.wire;
    wire->beginTransmission(_config.i2cAddress);
    wire->write(JOYSTICK2_RGB_REG);
    wire->write(reinterpret_cast<uint8_t*>(&color), 4);
    return wire->endTransmission() == 0;
}

void Joystick::calibrateCenter(int samples)
{
    uint32_t sumX = 0;
    uint32_t sumY = 0;
    int count = 0;
    for (int i = 0; i < samples; i++)
    {
        uint8_t adc[4];
        if (readRegister(JOYSTICK2_ADC_VALUE_12BITS_REG, adc, sizeof(adc)))
        {
            uint16_t x, y;
            memcpy(&x, &adc[0], 2);
            memcpy(&y, &adc[2], 2);
            sumX += x;
            sumY += y;
            count++;
        }
        delay(5);
    }
    // 1回も読めなかったら、中心は前の値のままにする
    if (count > 0)
    {
        _config.xCalibration.center = sumX / count;
        _config.yCalibration.center = sumY / count;
    }
}

int Joystick::normalize(uint16_t value, const AxisCalibration& cal) const
{
    const int lo = static_cast<int>(cal.min);
    const int center = static_cast<int>(cal.center);
    const int hi = static_cast<int>(cal.max);
    const int delta = static_cast<int>(value) - center;

    if (abs(delta) <= static_cast<int>(_config.deadzone))
    {
        return 0;
    }

    // 端の値を超えて読めたときも ±100 に収める
    if (delta > 0)
    {
        return min(100, (delta - _config.deadzone) * 100 / (hi - center - _config.deadzone));
    }

    return -min(100, (-delta - _config.deadzone) * 100 / (center - lo - _config.deadzone));
}
