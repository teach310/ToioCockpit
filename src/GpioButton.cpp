#include "GpioButton.hpp"

void GpioButton::update()
{
    _lastPressed = _currentPressed;
    _currentPressed = digitalRead(_pin) == LOW;
}
