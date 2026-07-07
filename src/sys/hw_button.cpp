#include "sys/hw_button.h"

HwButton::HwButton(uint8_t pin, bool activeLow)
    : _pin(pin), _activeLow(activeLow) {}

void HwButton::begin() {
  // Initialize Bounce2
  _bounce.attach(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
  _bounce.interval(10); // 10ms debounce time
  _bounce.setPressedState(_activeLow ? LOW : HIGH);
}

void HwButton::update() {
  _bounce.update();
  unsigned long currentMillis = millis();

  // 1. Button just pressed
  if (_bounce.pressed()) {
    if (_cbActivity)
      _cbActivity();
    _pressStartTime = currentMillis;
    _isLongPressing = false;
    _isHolding = false;
  }

  // 2. Button is currently being held down
  if (_bounce.isPressed()) {
    if (!_isLongPressing && (currentMillis - _pressStartTime >= _longPressMs)) {
      _isLongPressing = true; // Mark as long press so we don't trigger it twice
      _clickCount = 0;        // Cancel any pending double-click sequence
      if (_cbLongPressStart)
        _cbLongPressStart();
    }
    if (_holdMs > 0 && !_isHolding &&
        (currentMillis - _pressStartTime >= _holdMs)) {
      _isHolding = true;
      _clickCount = 0;
      if (_cbHold)
        _cbHold();
    }
  }

  // 3. Button just released
  if (_bounce.released()) {
    if (_cbActivity)
      _cbActivity();
    if (!_isLongPressing) {
      if (_cbDoubleClick) {
        _clickCount++;
        _lastReleaseTime = currentMillis;
      } else if (_cbSingleClick) {
        _cbSingleClick();
      }
    }
  }

  // 4. Button is currently released (Idle state) - Check for single/double
  // clicks
  if (!_bounce.isPressed() && _clickCount > 0) {
    if (currentMillis - _lastReleaseTime >= _doubleClickMs) {
      if (_clickCount == 1) {
        if (_cbSingleClick)
          _cbSingleClick();
      } else if (_clickCount >= 2) {
        if (_cbDoubleClick)
          _cbDoubleClick();
      }
      _clickCount = 0; // Reset after processing
    }
  }
}

// Setters
void HwButton::attachSingleClick(std::function<void()> callback) {
  _cbSingleClick = callback;
}
void HwButton::attachDoubleClick(std::function<void()> callback) {
  _cbDoubleClick = callback;
}
void HwButton::attachLongPressStart(std::function<void()> callback) {
  _cbLongPressStart = callback;
}
void HwButton::attachHold(std::function<void()> callback, uint16_t ms) {
  _cbHold = callback;
  _holdMs = ms;
}
void HwButton::attachActivity(std::function<void()> callback) {
  _cbActivity = callback;
}
void HwButton::setLongPressMs(uint16_t ms) { _longPressMs = ms; }
void HwButton::setDoubleClickMs(uint16_t ms) { _doubleClickMs = ms; }
