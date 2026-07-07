#ifndef HW_BUTTON_H
#define HW_BUTTON_H

#include <Arduino.h>
#include <Bounce2.h>
#include <functional>

/// Debounces one hardware button and emits single-click, double-click, long-press, and activity callbacks.
class HwButton {
public:
  /// Creates a button wrapper for a GPIO pin.
  HwButton(uint8_t pin, bool activeLow = true);

  /// Initializes GPIO and debounce state.
  void begin();

  /// Polls the button and emits pending callbacks.
  void update();

  /// Registers a callback for a completed single click.
  void attachSingleClick(std::function<void()> callback);
  /// Registers a callback for a completed double click.
  void attachDoubleClick(std::function<void()> callback);
  /// Registers a callback fired when long press begins.
  void attachLongPressStart(std::function<void()> callback);
  /// Registers a callback fired once when the button is held past a threshold.
  void attachHold(std::function<void()> callback, uint16_t ms);
  /// Registers a callback fired for any button activity.
  void attachActivity(std::function<void()> callback);

  /// Sets the long-press threshold in milliseconds.
  void setLongPressMs(uint16_t ms);
  /// Sets the maximum gap for double-click detection in milliseconds.
  void setDoubleClickMs(uint16_t ms);

private:
  uint8_t _pin;
  bool _activeLow;

  Bounce2::Button _bounce;

  // Timing configurations
  uint16_t _longPressMs = 5000;
  uint16_t _doubleClickMs = 500;
  uint16_t _holdMs = 0;

  // State tracking
  unsigned long _lastReleaseTime = 0;
  unsigned long _pressStartTime = 0;
  uint8_t _clickCount = 0;
  bool _isLongPressing = false;
  bool _isHolding = false;

  // Callbacks
  std::function<void()> _cbSingleClick = nullptr;
  std::function<void()> _cbDoubleClick = nullptr;
  std::function<void()> _cbLongPressStart = nullptr;
  std::function<void()> _cbHold = nullptr;
  std::function<void()> _cbActivity = nullptr;
};

#endif
