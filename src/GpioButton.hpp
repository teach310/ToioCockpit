#ifndef GPIO_BUTTON_HPP
#define GPIO_BUTTON_HPP

#include <Arduino.h>

class GpioButton
{
public:
    explicit GpioButton(uint8_t pin) : _pin(pin), _lastPressed(false), _currentPressed(false) {}

    void begin() { pinMode(_pin, INPUT); }
    void update();

    bool isPressed() const { return _currentPressed; }
    bool wasPressed() const { return !_lastPressed && _currentPressed; }
    bool wasReleased() const { return _lastPressed && !_currentPressed; }

private:
    uint8_t _pin;

    bool _lastPressed;
    bool _currentPressed;
};

#endif
