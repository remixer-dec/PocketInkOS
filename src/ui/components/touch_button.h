#ifndef TOUCH_BUTTON_H
#define TOUCH_BUTTON_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

struct UiRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

enum class TouchButtonShapeType : uint8_t {
  Rectangle,
  RoundedRectangle,
  Circle,
  Custom,
};

typedef bool (*TouchButtonHitTest)(const UiRect &bounds,
                                   const TouchPoint &point, void *context);
typedef void (*TouchButtonPainter)(Adafruit_GFX &gfx, const UiRect &bounds,
                                   bool pressed, void *context);

struct TouchButtonShape {
  TouchButtonShapeType type = TouchButtonShapeType::Rectangle;
  uint8_t radius = 0;
  TouchButtonHitTest hitTest = nullptr;
  TouchButtonPainter painter = nullptr;
  void *context = nullptr;

  static TouchButtonShape rectangle();
  static TouchButtonShape rounded(uint8_t radius);
  static TouchButtonShape circle();
  static TouchButtonShape custom(TouchButtonHitTest hitTest,
                                 TouchButtonPainter painter = nullptr,
                                 void *context = nullptr);
};

enum class TouchButtonIconPlacement : uint8_t {
  None,
  Only,
  AboveText,
  Left,
  Right,
};

enum class TouchButtonAlign : uint8_t {
  Start,
  Center,
  End,
};

struct TouchButtonContent {
  const char *text = nullptr;
  const char *icon = nullptr;
  TouchButtonIconPlacement iconPlacement = TouchButtonIconPlacement::None;
  bool iconUsesIconFont = true;
  uint8_t iconTextSize = 1;
  TouchButtonAlign textX = TouchButtonAlign::Center;
  TouchButtonAlign textY = TouchButtonAlign::Center;
  uint8_t textSize = 1;
  int8_t textOffsetX = 0;
  int8_t textOffsetY = 0;
  int8_t iconOffsetX = 0;
  int8_t iconOffsetY = 0;
};

struct TouchButtonStyle {
  uint8_t color = 1;
  uint8_t backgroundColor = 0;
  uint8_t pressedColor = 0;
  uint8_t pressedBackgroundColor = 1;
  uint8_t padding = 4;
  uint8_t touchPadding = 0;
  bool drawOutline = true;
  bool fillPressed = true;
};

class TouchButton {
public:
  TouchButton(const UiRect &bounds,
              TouchButtonShape shape = TouchButtonShape::rectangle());
  TouchButton(const UiRect &bounds, const TouchButtonContent &content,
              TouchButtonShape shape = TouchButtonShape::rectangle(),
              const TouchButtonStyle &style = TouchButtonStyle());

  bool contains(const TouchPoint &point) const;
  void draw(Adafruit_GFX &gfx, bool pressed = false) const;

  const UiRect &bounds() const;

private:
  UiRect _bounds;
  TouchButtonShape _shape;
  TouchButtonContent _content;
  TouchButtonStyle _style;
};

#endif
