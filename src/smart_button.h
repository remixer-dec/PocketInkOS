#ifndef SMART_BUTTON_H
#define SMART_BUTTON_H

#include <Arduino.h>
#include <Bounce2.h>
#include <functional>

class SmartButton {
public:
    // Constructor
    SmartButton(uint8_t pin, bool activeLow = true);

    // Initialization to be called in setup()
    void begin();

    // The update loop to be called in loop()
    void update();

    // Callback setters
    void attachSingleClick(std::function<void()> callback);
    void attachDoubleClick(std::function<void()> callback);
    void attachLongPressStart(std::function<void()> callback);

    // Configuration settings
    void setLongPressMs(uint16_t ms);
    void setDoubleClickMs(uint16_t ms);

private:
    uint8_t _pin;
    bool _activeLow;
    
    Bounce2::Button _bounce;

    // Timing configurations
    uint16_t _longPressMs = 5000;
    uint16_t _doubleClickMs = 500;

    // State tracking
    unsigned long _lastReleaseTime = 0;
    unsigned long _pressStartTime = 0;
    uint8_t _clickCount = 0;
    bool _isLongPressing = false;

    // Callbacks
    std::function<void()> _cbSingleClick = nullptr;
    std::function<void()> _cbDoubleClick = nullptr;
    std::function<void()> _cbLongPressStart = nullptr;
};

#endif