#ifndef SELECT_BUTTON_COMPONENT_H
#define SELECT_BUTTON_COMPONENT_H

#include "ui/ui_helpers.h"
#include <stdint.h>

struct SelectButtonOption {
  UiRect rect;
  const char *label;
  int8_t value;
};

class SelectButtonGroup {
public:
  SelectButtonGroup(const SelectButtonOption *options, uint8_t count)
      : _options(options), _count(count) {}

  void draw(Adafruit_GFX &gfx, int8_t selectedValue,
            const UiPressFeedback *press = nullptr,
            uint8_t textSize = 1) const {
    for (uint8_t i = 0; i < _count; i++) {
      const SelectButtonOption &option = _options[i];
      const bool active =
          option.value == selectedValue ||
          (press != nullptr && press->isPressed(option.value));
      uiDrawButton(gfx, option.rect, option.label, active, textSize);
    }
  }

  bool hitTest(const TouchPoint &point, int8_t &value,
               uint8_t touchPadding = UI_TOUCH_ASSIST_PX) const {
    for (uint8_t i = 0; i < _count; i++) {
      const SelectButtonOption &option = _options[i];
      if (uiTouchContains(option.rect, point, touchPadding)) {
        value = option.value;
        return true;
      }
    }
    return false;
  }

private:
  const SelectButtonOption *_options;
  uint8_t _count;
};

#endif
