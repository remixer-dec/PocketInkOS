#include "smart_button.h"

SmartButton::SmartButton(uint8_t pin, bool activeLow) 
    : _pin(pin), _activeLow(activeLow) {}

void SmartButton::begin() {
    // Initialize Bounce2
    _bounce.attach(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    _bounce.interval(10); // 10ms debounce time
    _bounce.setPressedState(_activeLow ? LOW : HIGH);
}

void SmartButton::update() {
    _bounce.update();
    unsigned long currentMillis = millis();

    // 1. Button just pressed
    if (_bounce.pressed()) {
        _pressStartTime = currentMillis;
        _isLongPressing = false;
    }

    // 2. Button is currently being held down
    if (_bounce.isPressed()) {
        if (!_isLongPressing && (currentMillis - _pressStartTime >= _longPressMs)) {
            _isLongPressing = true; // Mark as long press so we don't trigger it twice
            _clickCount = 0;        // Cancel any pending double-click sequence
            if (_cbLongPressStart) _cbLongPressStart();
        }
    }

    // 3. Button just released
    if (_bounce.released()) {
        if (!_isLongPressing) {
            // It was a short press
            _clickCount++;
            _lastReleaseTime = currentMillis;
        }
    }

    // 4. Button is currently released (Idle state) - Check for single/double clicks
    if (!_bounce.isPressed() && _clickCount > 0) {
        if (currentMillis - _lastReleaseTime >= _doubleClickMs) {
            if (_clickCount == 1) {
                if (_cbSingleClick) _cbSingleClick();
            } 
            else if (_clickCount >= 2) {
                if (_cbDoubleClick) _cbDoubleClick();
            }
            _clickCount = 0; // Reset after processing
        }
    }
}

// Setters
void SmartButton::attachSingleClick(std::function<void()> callback) { _cbSingleClick = callback; }
void SmartButton::attachDoubleClick(std::function<void()> callback) { _cbDoubleClick = callback; }
void SmartButton::attachLongPressStart(std::function<void()> callback) { _cbLongPressStart = callback; }
void SmartButton::setLongPressMs(uint16_t ms) { _longPressMs = ms; }
void SmartButton::setDoubleClickMs(uint16_t ms) { _doubleClickMs = ms; }